/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

"use strict";
// Make this available to both AMD and CJS environments
define(function(require, exports, module) {
  // Dependencies
  const PropTypes = require("devtools/client/shared/vendor/react-prop-types");
  const { span } = require("devtools/client/shared/vendor/react-dom-factories");

  const {
    wrapRender,
  } = require("devtools/client/shared/components/reps/reps/rep-utils");

  /**
   * Renders a caption. This template is used by other components
   * that needs to distinguish between a simple text/value and a label.
   */

  // TODO: Is this file actually used anywhere? I can't seem to find a reference to the caption function...

  Caption.propTypes = {
    object: PropTypes.oneOfType([PropTypes.number, PropTypes.string])
      .isRequired,
  };

  function Caption(props) {
    return span({ className: "caption" }, props.object);
  }

  // Exports from this module
  module.exports = wrapRender(Caption);
});
