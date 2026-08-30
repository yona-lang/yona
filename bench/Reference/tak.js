function tak(x, y, z) {
    if (y >= x) return z;
    return tak(tak(x-1, y, z), tak(y-1, z, x), tak(z-1, x, y));
}
console.log(tak(30, 20, 10));
