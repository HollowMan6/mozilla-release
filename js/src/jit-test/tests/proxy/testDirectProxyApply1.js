// Forward to the target if the trap is undefined
var target = function (x, y) {
    return x + y;
}
assertEq(Proxy(target, {})(2, 3), 5);
