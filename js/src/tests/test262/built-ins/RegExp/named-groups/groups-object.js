// Copyright 2017 Aleksey Shvayka. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: Properties of the groups object are created with CreateDataProperty
includes: [propertyHelper.js]
esid: sec-regexpbuiltinexec
features: [regexp-named-groups]
info: |
  Runtime Semantics: RegExpBuiltinExec ( R, S )
    24. If _R_ contains any |GroupName|, then
      a. Let _groups_ be ObjectCreate(*null*).
    25. Else,
      a. Let _groups_ be *undefined*.
    26. Perform ! CreateDataProperty(_A_, `"groups"`, _groups_).
---*/

// `groups` is created with Define, not Set.
let counter = 0;
Object.defineProperty(Array.prototype, "groups", {
  set() { counter++; }
});

let match = /(?<x>.)/.exec("a");
assert.sameValue(counter, 0);

// `groups` is writable, enumerable and configurable
// (from CreateDataProperty).
verifyProperty(match, "groups", {
  writable: true,
  enumerable: true,
  configurable: true,
});

reportCompare(0, 0);
