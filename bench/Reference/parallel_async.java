import java.util.concurrent.*;

public class parallel_async {
    static int slow(int x) throws InterruptedException { Thread.sleep(x); return x; }
    public static void main(String[] args) throws Exception {
        ExecutorService ex = Executors.newFixedThreadPool(4);
        Future<Integer> a = ex.submit(() -> slow(100));
        Future<Integer> b = ex.submit(() -> slow(100));
        Future<Integer> c = ex.submit(() -> slow(100));
        Future<Integer> d = ex.submit(() -> slow(100));
        int sum = a.get() + b.get() + c.get() + d.get();
        ex.shutdown();
        System.out.println(sum);
    }
}
