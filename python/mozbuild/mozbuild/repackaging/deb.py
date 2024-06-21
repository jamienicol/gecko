# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import datetime
import json
import logging
import os
import shutil
import subprocess
import tarfile
import tempfile
import zipfile
from email.utils import format_datetime
from pathlib import Path
from string import Template

import mozfile
import mozpack.path as mozpath
import requests
from mozilla_version.gecko import GeckoVersion
from redo import retry

from mozbuild.repackaging.application_ini import get_application_ini_values


class NoDebPackageFound(Exception):
    """Raised when no .deb is found after calling dpkg-buildpackage"""

    def __init__(self, deb_file_path) -> None:
        super().__init__(
            f"No {deb_file_path} package found after calling dpkg-buildpackage"
        )


class HgServerError(Exception):
    """Raised when Hg responds with an error code that is not 404 (i.e. when there is an outage)"""

    def __init__(self, msg) -> None:
        super().__init__(msg)


# Maps our CI/release pipeline's architecture names (e.g., "x86_64")
# into architectures ("amd64") compatible with Debian's dpkg-buildpackage tool.
# This is the target architecture we are building the .deb package for.
_DEB_ARCH = {
    "all": "all",
    "x86": "i386",
    "x86_64": "amd64",
    "aarch64": "arm64",
}

# Defines the sysroot (build host's) architecture for each target architecture in the pipeline.
# It defines the architecture dpkg-buildpackage runs on.
_DEB_SYSROOT_ARCH = {
    "all": "amd64",
    "x86": "i386",
    "x86_64": "amd64",
    "aarch64": "amd64",
}

# Assigns the Debian distribution version for the sysroot based on the target architecture.
# It defines the Debian distribution dpkg-buildpackage runs on.
_DEB_SYSROOT_DIST = {
    "all": "jessie",
    "x86": "jessie",
    "x86_64": "jessie",
    "aarch64": "buster",
}


def repackage_deb(
    log,
    infile,
    output,
    template_dir,
    arch,
    version,
    build_number,
    release_product,
    release_type,
    fluent_localization,
    fluent_resource_loader,
):
    if not tarfile.is_tarfile(infile):
        raise Exception("Input file %s is not a valid tarfile." % infile)

    tmpdir = _create_temporary_directory(arch)
    source_dir = os.path.join(tmpdir, "source")
    try:
        mozfile.extract_tarball(infile, source_dir)
        application_ini_data = _load_application_ini_data(infile, version, build_number)
        build_variables = _get_build_variables(
            application_ini_data,
            arch,
            depends="${shlibs:Depends},",
            release_product=release_product,
        )

        _copy_plain_deb_config(template_dir, source_dir)
        _render_deb_templates(
            template_dir,
            source_dir,
            build_variables,
            exclude_file_names=["package-prefs.js"],
        )

        app_name = application_ini_data["name"]
        with open(
            mozpath.join(source_dir, app_name.lower(), "is-packaged-app"), "w"
        ) as f:
            f.write("This is a packaged app.\n")

        _inject_deb_distribution_folder(source_dir, app_name)
        _inject_deb_desktop_entry_file(
            log,
            source_dir,
            build_variables,
            release_product,
            release_type,
            fluent_localization,
            fluent_resource_loader,
        )
        _inject_deb_prefs_file(source_dir, app_name, template_dir)
        _generate_deb_archive(
            source_dir,
            target_dir=tmpdir,
            output_file_path=output,
            build_variables=build_variables,
            arch=arch,
        )

    finally:
        shutil.rmtree(tmpdir)


def repackage_deb_l10n(
    input_xpi_file,
    input_tar_file,
    output,
    template_dir,
    version,
    build_number,
    release_product,
):
    arch = "all"

    tmpdir = _create_temporary_directory(arch)
    source_dir = os.path.join(tmpdir, "source")
    try:
        langpack_metadata = _extract_langpack_metadata(input_xpi_file)
        langpack_dir = mozpath.join(source_dir, "firefox", "distribution", "extensions")
        application_ini_data = _load_application_ini_data(
            input_tar_file, version, build_number
        )
        langpack_id = langpack_metadata["langpack_id"]
        if release_product == "devedition":
            depends = (
                f"firefox-devedition (= {application_ini_data['deb_pkg_version']})"
            )
        else:
            depends = f"{application_ini_data['remoting_name']} (= {application_ini_data['deb_pkg_version']})"
        build_variables = _get_build_variables(
            application_ini_data,
            arch,
            depends=depends,
            # Debian package names are only lowercase
            package_name_suffix=f"-l10n-{langpack_id.lower()}",
            description_suffix=f" - {langpack_metadata['description']}",
            release_product=release_product,
        )
        _copy_plain_deb_config(template_dir, source_dir)
        _render_deb_templates(template_dir, source_dir, build_variables)

        os.makedirs(langpack_dir, exist_ok=True)
        shutil.copy(
            input_xpi_file,
            mozpath.join(
                langpack_dir,
                f"{langpack_metadata['browser_specific_settings']['gecko']['id']}.xpi",
            ),
        )
        _generate_deb_archive(
            source_dir=source_dir,
            target_dir=tmpdir,
            output_file_path=output,
            build_variables=build_variables,
            arch=arch,
        )
    finally:
        shutil.rmtree(tmpdir)


def _extract_application_ini_data(input_tar_file):
    with tempfile.TemporaryDirectory() as d:
        with tarfile.open(input_tar_file) as tar:
            application_ini_files = [
                tar_info
                for tar_info in tar.getmembers()
                if tar_info.name.endswith("/application.ini")
            ]
            if len(application_ini_files) == 0:
                raise ValueError(
                    f"Cannot find any application.ini file in archive {input_tar_file}"
                )
            if len(application_ini_files) > 1:
                raise ValueError(
                    f"Too many application.ini files found in archive {input_tar_file}. "
                    f"Found: {application_ini_files}"
                )

            tar.extract(application_ini_files[0], path=d)

        application_ini_data = _extract_application_ini_data_from_directory(d)

        return application_ini_data


def _load_application_ini_data(infile, version, build_number):
    extracted_application_ini_data = _extract_application_ini_data(infile)
    parsed_application_ini_data = _parse_application_ini_data(
        extracted_application_ini_data, version, build_number
    )
    return parsed_application_ini_data


def _parse_application_ini_data(application_ini_data, version, build_number):
    application_ini_data["timestamp"] = datetime.datetime.strptime(
        application_ini_data["build_id"], "%Y%m%d%H%M%S"
    )

    application_ini_data["remoting_name"] = application_ini_data[
        "remoting_name"
    ].lower()

    application_ini_data["deb_pkg_version"] = _get_deb_pkg_version(
        version, application_ini_data["build_id"], build_number
    )

    return application_ini_data


def _get_deb_pkg_version(version, build_id, build_number):
    gecko_version = GeckoVersion.parse(version)
    deb_pkg_version = (
        f"{gecko_version}~{build_id}"
        if gecko_version.is_nightly
        else f"{gecko_version}~build{build_number}"
    )
    return deb_pkg_version


def _extract_application_ini_data_from_directory(application_directory):
    values = get_application_ini_values(
        application_directory,
        dict(section="App", value="Name"),
        dict(section="App", value="CodeName", fallback="Name"),
        dict(section="App", value="Vendor"),
        dict(section="App", value="RemotingName"),
        dict(section="App", value="BuildID"),
    )

    data = {
        "name": next(values),
        "display_name": next(values),
        "vendor": next(values),
        "remoting_name": next(values),
        "build_id": next(values),
    }

    return data


def _get_build_variables(
    application_ini_data,
    arch,
    depends,
    package_name_suffix="",
    description_suffix="",
    release_product="",
):
    if release_product == "devedition":
        deb_pkg_install_path = "usr/lib/firefox-devedition"
        deb_pkg_name = f"firefox-devedition{package_name_suffix}"
    else:
        deb_pkg_install_path = f"usr/lib/{application_ini_data['remoting_name']}"
        deb_pkg_name = f"{application_ini_data['remoting_name']}{package_name_suffix}"
    return {
        "DEB_DESCRIPTION": f"{application_ini_data['vendor']} {application_ini_data['display_name']}"
        f"{description_suffix}",
        "DEB_PKG_INSTALL_PATH": deb_pkg_install_path,
        "DEB_PKG_NAME": deb_pkg_name,
        "DEB_PKG_VERSION": application_ini_data["deb_pkg_version"],
        "DEB_CHANGELOG_DATE": format_datetime(application_ini_data["timestamp"]),
        "DEB_ARCH_NAME": _DEB_ARCH[arch],
        "DEB_DEPENDS": depends,
    }


def _copy_plain_deb_config(input_template_dir, source_dir):
    template_dir_filenames = os.listdir(input_template_dir)
    plain_filenames = [
        mozpath.basename(filename)
        for filename in template_dir_filenames
        if not filename.endswith(".in") and not filename.endswith(".js")
    ]
    os.makedirs(mozpath.join(source_dir, "debian"), exist_ok=True)

    for filename in plain_filenames:
        shutil.copy(
            mozpath.join(input_template_dir, filename),
            mozpath.join(source_dir, "debian", filename),
        )


def _render_deb_templates(
    input_template_dir, source_dir, build_variables, exclude_file_names=None
):
    exclude_file_names = [] if exclude_file_names is None else exclude_file_names

    template_dir_filenames = os.listdir(input_template_dir)
    template_filenames = [
        mozpath.basename(filename)
        for filename in template_dir_filenames
        if filename.endswith(".in") and filename not in exclude_file_names
    ]
    os.makedirs(mozpath.join(source_dir, "debian"), exist_ok=True)

    for file_name in template_filenames:
        with open(mozpath.join(input_template_dir, file_name)) as f:
            template = Template(f.read())
        with open(mozpath.join(source_dir, "debian", Path(file_name).stem), "w") as f:
            f.write(template.substitute(build_variables))


def _inject_deb_distribution_folder(source_dir, app_name):
    with tempfile.TemporaryDirectory() as git_clone_dir:
        subprocess.check_call(
            [
                "git",
                "clone",
                "https://github.com/mozilla-partners/deb.git",
                git_clone_dir,
            ],
        )
        shutil.copytree(
            mozpath.join(git_clone_dir, "desktop/deb/distribution"),
            mozpath.join(source_dir, app_name.lower(), "distribution"),
        )


def _inject_deb_prefs_file(source_dir, app_name, template_dir):
    src = mozpath.join(template_dir, "package-prefs.js")
    dst = mozpath.join(source_dir, app_name.lower(), "defaults/pref")
    shutil.copy(src, dst)


def _inject_deb_desktop_entry_file(
    log,
    source_dir,
    build_variables,
    release_product,
    release_type,
    fluent_localization,
    fluent_resource_loader,
):
    desktop_entry_file_text = _generate_browser_desktop_entry_file_text(
        log,
        build_variables,
        release_product,
        release_type,
        fluent_localization,
        fluent_resource_loader,
    )
    desktop_entry_file_filename = f"{build_variables['DEB_PKG_NAME']}.desktop"
    os.makedirs(mozpath.join(source_dir, "debian"), exist_ok=True)
    with open(
        mozpath.join(source_dir, "debian", desktop_entry_file_filename), "w"
    ) as f:
        f.write(desktop_entry_file_text)


def _generate_browser_desktop_entry_file_text(
    log,
    build_variables,
    release_product,
    release_type,
    fluent_localization,
    fluent_resource_loader,
):
    localizations = _create_fluent_localizations(
        fluent_resource_loader, fluent_localization, release_type, release_product, log
    )
    desktop_entry = _generate_browser_desktop_entry(build_variables, localizations)
    desktop_entry_file_text = "\n".join(desktop_entry)
    return desktop_entry_file_text


def _create_fluent_localizations(
    fluent_resource_loader, fluent_localization, release_type, release_product, log
):
    brand_fluent_filename = "brand.ftl"
    l10n_central_url = "https://raw.githubusercontent.com/mozilla-l10n/firefox-l10n"
    desktop_entry_fluent_filename = "linuxDesktopEntry.ftl"

    l10n_dir = tempfile.mkdtemp()

    loader = fluent_resource_loader(os.path.join(l10n_dir, "{locale}"))

    localizations = {}
    linux_l10n_changesets = _load_linux_l10n_changesets(
        "browser/locales/l10n-changesets.json"
    )
    locales = ["en-US"]
    locales.extend(linux_l10n_changesets.keys())
    en_US_brand_fluent_filename = _get_en_US_brand_fluent_filename(
        brand_fluent_filename, release_type, release_product
    )

    for locale in locales:
        locale_dir = os.path.join(l10n_dir, locale)
        os.mkdir(locale_dir)
        localized_desktop_entry_filename = os.path.join(
            locale_dir, desktop_entry_fluent_filename
        )
        if locale == "en-US":
            en_US_desktop_entry_fluent_filename = os.path.join(
                "browser", "locales", "en-US", "browser", desktop_entry_fluent_filename
            )
            shutil.copyfile(
                en_US_desktop_entry_fluent_filename,
                localized_desktop_entry_filename,
            )
        else:
            non_en_US_fluent_resource_file_url = f"{l10n_central_url}/{linux_l10n_changesets[locale]['revision']}/{locale}/browser/browser/{desktop_entry_fluent_filename}"
            response = requests.get(non_en_US_fluent_resource_file_url)
            response = retry(
                requests.get,
                args=[non_en_US_fluent_resource_file_url],
                attempts=5,
                sleeptime=3,
                jitter=2,
            )
            mgs = "Missing {fluent_resource_file_name} for {locale}: received HTTP {status_code} for GET {resource_file_url}"
            params = {
                "fluent_resource_file_name": desktop_entry_fluent_filename,
                "locale": locale,
                "resource_file_url": non_en_US_fluent_resource_file_url,
                "status_code": response.status_code,
            }
            action = "repackage-deb"
            if response.status_code == 404:
                log(
                    logging.WARNING,
                    action,
                    params,
                    mgs,
                )
                continue
            if response.status_code != 200:
                log(
                    logging.ERROR,
                    action,
                    params,
                    mgs,
                )
                raise HgServerError(mgs.format(**params))

            with open(localized_desktop_entry_filename, "w", encoding="utf-8") as f:
                f.write(response.text)

        shutil.copyfile(
            en_US_brand_fluent_filename,
            os.path.join(locale_dir, brand_fluent_filename),
        )

        fallbacks = [locale]
        if locale != "en-US":
            fallbacks.append("en-US")
        localizations[locale] = fluent_localization(
            fallbacks, [desktop_entry_fluent_filename, brand_fluent_filename], loader
        )

    return localizations


def _get_en_US_brand_fluent_filename(
    brand_fluent_filename, release_type, release_product
):
    branding_fluent_filename_template = os.path.join(
        "browser/branding/{brand}/locales/en-US", brand_fluent_filename
    )
    if release_type == "nightly":
        return branding_fluent_filename_template.format(brand="nightly")
    elif release_type == "release" or release_type == "release-rc":
        return branding_fluent_filename_template.format(brand="official")
    elif release_type == "beta" and release_product == "firefox":
        return branding_fluent_filename_template.format(brand="official")
    elif release_type == "beta" and release_product == "devedition":
        return branding_fluent_filename_template.format(brand="aurora")
    elif release_type.startswith("esr"):
        return branding_fluent_filename_template.format(brand="official")
    el                   branding_fluent_filename_template.format(brand="unofficial")


def _load_linux_l10n_changesets(l10n_changesets_filename):
    with open(l10n_changesets_filename) as l10n_changesets_file:
        l10n_changesets = json.load(l10n_changesets_file)
        return {
            locale: changeset
            for locale, changeset in l10n_changesets.items()
            if any(platform.startswith("linux") for platform in changeset["platforms"])
        }


def _generate_browser_desktop_entry(build_variables, localizations):
    mime_types = [
        "application/json",
        "application/pdf",
        "application/rdf+xml",
        "application/rss+xml",
        "application/x-xpinstall",
        "application/xhtml+xml",
        "application/xml",
        "audio/flac",
        "audio/ogg",
        "audio/webm",
        "image/avif",
        "image/gif",
        "image/jpeg",
        "image/png",
        "image/svg",
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             