class tak {
    static long tak(long x, long y, long z) {
        if (y >= x) return z;
        return tak(tak(x-1, y, z), tak(y-1, z, x), tak(z-1, x, y));
    }
    public static void main(String[] args) { System.out.println(tak(30, 20, 10)); }
}
