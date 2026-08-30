class sum_squares {
    public static void main(String[] args) {
        long n = 1000000, acc = 0;
        while (n > 0) { acc += n * n; n--; }
        System.out.println(acc);
    }
}
