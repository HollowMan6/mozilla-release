// |jit-test| error: ExitCleanly

// proxies can return primitives
assertEq(new (Proxy.createFunction({}, function(){}, function(){})), undefined);
x = Proxy.createFunction((function () {}), Uint16Array, wrap)
new(wrap(x))
assertEq((new (Proxy.createFunction({},
                                    function(){ this.x = 1 },
                                    function(){ this.x = 2 }))).x, 2);
try {
    x = Proxy.createFunction((function () {}), Uint16Array, wrap)
    new(wrap(x))
    throw "Should not be reached"
}
catch (e) {
    assertEq(String(e.message).indexOf('is not a constructor') === -1, false);
}
// proxies can return the callee
var x = Proxy.createFunction({}, function (q) { return q; });
assertEq(new x(x), x);
try {
    var x = (Proxy.createFunction({}, "".indexOf));
    new x;
    throw "Should not be reached"
}
catch (e) {
    assertEq(e.message, 'new x is not a constructor');
}
throw "ExitCleanly"
