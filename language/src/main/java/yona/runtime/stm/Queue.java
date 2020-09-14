package yona.runtime.stm;

import java.util.NoSuchElementException;

// mutable array-based queue with capacity being a power of two
final class Queue<E> {
    private Object[] elements;
    private long head;
    private long tail;

    public Queue(final int initialCapacity) {
        if (Integer.bitCount(initialCapacity) != 1 || initialCapacity == 1) {
            throw new AssertionError();
        }
        elements = new Object[initialCapacity];
    }

    public void enqueue(final E e) {
        ensureCapacity(1);
        elements[idx(tail)] = e;
        tail++;
    }

    public E dequeue() {
        final int idx = idx(head);
        final E result = elementAt(idx);
        if (result == null) {
            throw new NoSuchElementException();
        }
        elements[idx] = null;
        head++;
        return result;
    }

    public E head() {
        final E result = elementAt(idx(head));
        if (result == null) {
            throw new NoSuchElementException();
        }
        return result;
    }

    private int idx(final long value) {
        return (int) (value & (elements.length - 1));
    }

    @SuppressWarnings("unchecked")
    private E elementAt(final int idx) {
        return (E) elements[idx];
    }

    public void ensureCapacity(int n) {
        final int length = length();
        n -= (elements.length - length);
        if (n > 0) {
            final Object[] newElements = new Object[nextPowerOfTwo(elements.length + n)];
            final int headIdx = idx(head);
            final int tailIdx = idx(tail);
            if (headIdx >= tailIdx) {
                System.arraycopy(elements, headIdx, newElements, 0, elements.length - headIdx);
                System.arraycopy(elements, 0, newElements, elements.length - headIdx, tailIdx);
            } else {
                System.arraycopy(elements, headIdx, newElements, 0, length);
            }
            elements = newElements;
            head = 0;
            tail = length;
        }
    }

    static int nextPowerOfTwo(final int value) {
        final int n = Integer.numberOfLeadingZeros(value - 1);
        return 1 << -n;
    }

    public int length() {
        return (int) (tail - head);
    }
}
