import java.util.concurrent.*;

public class channel_fanin {
    static final Integer SENTINEL = Integer.MIN_VALUE;
    public static void main(String[] args) throws Exception {
        BlockingQueue<Integer> work = new ArrayBlockingQueue<>(32);
        BlockingQueue<Integer> coord = new ArrayBlockingQueue<>(4);

        Thread pa = new Thread(() -> {
            try { for (int n = 1;    n <= 2500; n++) work.put(n); coord.put(1); }
            catch (InterruptedException ignored) {}
        });
        Thread pb = new Thread(() -> {
            try { for (int n = 2501; n <= 5000; n++) work.put(n); coord.put(1); }
            catch (InterruptedException ignored) {}
        });
        Thread closer = new Thread(() -> {
            try {
                coord.take();
                coord.take();
                work.put(SENTINEL);
            } catch (InterruptedException ignored) {}
        });
        pa.start(); pb.start(); closer.start();

        long sum = 0;
        Integer v;
        while (!(v = work.take()).equals(SENTINEL)) sum += v;
        pa.join(); pb.join(); closer.join();
        System.out.println(sum);
    }
}
