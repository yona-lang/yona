public class int_array_map {
    public static void main(String[] args) {
        int[] a = new int[10000];
        for (int i = 0; i < 10000; i++) a[i] = i + 1;
        int[] doubled = new int[10000];
        for (int i = 0; i < 10000; i++) doubled[i] = a[i] * 2;
        long s = 0;
        for (int x : doubled) s += x;
        System.out.println(s);
    }
}
