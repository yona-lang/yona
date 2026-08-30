class sieve {
    public static void main(String[] args) {
        int n = 100000;
        boolean[] is_prime = new boolean[n + 1];
        java.util.Arrays.fill(is_prime, true);
        is_prime[0] = is_prime[1] = false;
        for (int i = 2; i * i <= n; i++)
            if (is_prime[i]) for (int j = i*i; j <= n; j += i) is_prime[j] = false;
        int count = 0;
        for (boolean b : is_prime) if (b) count++;
        System.out.println(count);
    }
}
