class ackermann {
    static long ack(long m, long n) {
        if (m == 0) return n + 1;
        if (n == 0) return ack(m - 1, 1);
        return ack(m - 1, ack(m, n - 1));
    }
    public static void main(String[] args) { System.out.println(ack(3, 10)); }
}
