package yona.runtime.stm;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class QueueTest {
    static final int N = 15000;
    static final int D = 7;

    @Test
    public void testEnqueueDequeue() {
        for (int i = 1; i < N; i++) {
            Queue<Integer> q = new Queue<>(2);
            for (int j = 0; j < i / D; j++) {
                q.enqueue(-1);
            }
            for (int j = 0; j < i / D; j++) {
                assertEquals(-1, q.dequeue());
            }
            assertEquals(0, q.length());
            for (int j = 0; j < i; j++) {
                assertEquals(j, q.length());
                if (j % D == 0) {
                    q.ensureCapacity(2);
                }
                q.enqueue(j);
                assertEquals(j + 1, q.length());
            }
            for (int j = 0; j < i; j++) {
                assertEquals(i - j, q.length());
                assertEquals(j, q.head());
                assertEquals(j, q.dequeue());
                assertEquals(i - j - 1, q.length());
            }
        }
    }
}
