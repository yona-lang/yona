public class sequential_async {
    static int slow(int x) throws InterruptedException { Thread.sleep(x); return x; }
    public static void main(String[] args) throws Exception {
        int a = slow(100);
        int b = slow(a);
        int c = slow(b);
        int d = slow(c);
        System.out.println(d);
    }
}
