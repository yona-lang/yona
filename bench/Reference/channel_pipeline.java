import java.util.concurrent.*;

public class channel_pipeline {
    static final Integer SENTINEL = Integer.MIN_VALUE;
    public static void main(String[] args) throws Exception {
        BlockingQueue<Integer> ch = new ArrayBlockingQueue<>(32);
        Thread producer = new Thread(() -> {
            try {
                for (int n = 1; n <= 5000; n++) ch.put(n * 2);
                ch.put(SENTINEL);
            } catch (InterruptedException ignored) {}
        });
        producer.start();
        long sum = 0;
        Integer v;
        while (!(v = ch.take()).equals(SENTINEL)) sum += v;
        producer.join();
        System.out.println(sum);
    }
}
