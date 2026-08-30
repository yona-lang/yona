class queens {
    static boolean safe(int queen, int[] placed, int n, int col) {
        if (n == 0) return true;
        if (queen == placed[0] || Math.abs(queen - placed[0]) == col) return false;
        int[] rest = new int[n - 1];
        System.arraycopy(placed, 1, rest, 0, n - 1);
        return safe(queen, rest, n - 1, col + 1);
    }
    static long solve(int n, int row, int[] placed, int nplaced) {
        if (row > n) return 1;
        long count = 0;
        for (int col = 1; col <= n; col++) {
            if (safe(col, placed, nplaced, 1)) {
                int[] next = new int[nplaced + 1];
                next[0] = col;
                System.arraycopy(placed, 0, next, 1, nplaced);
                count += solve(n, row + 1, next, nplaced + 1);
            }
        }
        return count;
    }
    public static void main(String[] args) {
        System.out.println(solve(10, 1, new int[0], 0));
    }
}
