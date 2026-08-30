public class int_array_sum {
    public static void main(String[] args) {
        int[] a = new int[10000];
        for (int i = 0; i < 10000; i++) a[i] = i + 1;
        long s = 0;
        for (int x : a) s += x;
        System.out.println(s);
    }
}
