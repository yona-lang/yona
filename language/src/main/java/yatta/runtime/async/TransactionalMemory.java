package yatta.runtime.async;

import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.nodes.Node;
import yatta.YattaException;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;
import java.lang.ref.PhantomReference;
import java.lang.ref.ReferenceQueue;
import java.lang.ref.WeakReference;

import static java.lang.System.currentTimeMillis;
import static java.lang.System.identityHashCode;
import static java.util.Arrays.copyOf;
import static java.util.Arrays.fill;

public final class TransactionalMemory implements TruffleObject {
  static final int WRITE_SET_INITIAL_CAPACITY = 2;
  static final int WRITE_SET_PROBES = 4;
  static final int BLOOM_FILTER_HASHES = 7;

  static final VarHandle VAR_REFS_HANDLE;

  static {
    try {
      VAR_REFS_HANDLE = MethodHandles.lookup().findVarHandle(TransactionalMemory.class, "varRefs", WeakReference[].class);
    } catch (NoSuchFieldException | IllegalAccessException e) {
      throw new AssertionError(e);
    }
  }
  
  volatile TransactionsRecord lastCommittedRecord = new TransactionsRecord();
  final ReferenceQueue<TransactionsRecord> recordsQueue = new ReferenceQueue<>();
  final ReferenceQueue<Var> varsQueue = new ReferenceQueue<>();
  volatile WeakReference<Var>[] varRefs;

  public TransactionalMemory() {
    //noinspection unchecked
    varRefs = new WeakReference[0];
    Thread gcThread = new Thread(() -> {
      while (true) {
        try {
          final long stamp = ((TransactionsRecord) recordsQueue.remove()).stamp - 1;
          for (WeakReference<Var> ref : varRefs) {
            final Var var = ref.get();
            if (var != null) {
              cleanUp(var, stamp);
            }
          }
          expungeStaleVarRefs();
        } catch (InterruptedException e) {
          break;
        }
      }
    });
    gcThread.setDaemon(true);
    gcThread.start();
  }

  void expungeStaleVarRefs() {
    WeakReference<Var> old;
    //noinspection unchecked
    while ((old = (WeakReference<Var>) varsQueue.poll()) != null) {
      deregisterVar(old);
      old.clear();
    }
  }

  void registerVar(final WeakReference<Var> ref) {
    WeakReference<Var>[] expected;
    WeakReference<Var>[] updated;
    do {
      expected = varRefs;
      updated = copyOf(expected, expected.length + 1);
      updated[expected.length] = ref;
    } while (!VAR_REFS_HANDLE.compareAndSet(this, expected, updated));
  }

  void deregisterVar(final WeakReference<Var> ref) {
    WeakReference<Var>[] expected;
    WeakReference<Var>[] updated;
    do {
      expected = varRefs;
      //noinspection unchecked
      updated = new WeakReference[expected.length - 1];
      int updatedIdx = 0;
      for (WeakReference<Var> element : expected) {
        if (ref != element) {
          updated[updatedIdx++] = element;
        }
      }
    } while (!VAR_REFS_HANDLE.compareAndSet(this, expected, updated));
  }

  void cleanUp(final Var var, final long stamp) {
    Var.Box top = var.current;
    Var.Box bottom = top.prev;
    while (bottom != null) {
      if (bottom.stamp < stamp) {
        top.prev = null;
        break;
      } else {
        top = bottom;
        bottom = top.prev;
      }
    }
  }

  static final class TransactionsRecord extends PhantomReference<TransactionsRecord> {
    static final VarHandle NEXT_HANDLE;

    static {
      try {
        NEXT_HANDLE = MethodHandles.lookup().findVarHandle(TransactionsRecord.class, "next", TransactionsRecord.class);
      } catch (NoSuchFieldException | IllegalAccessException e) {
        throw new AssertionError(e);
      }
    }

    final long stamp;
    final long[] writeFilter;
    final long writeFilterSummary;
    volatile Status status = Status.VALID;
    volatile TransactionsRecord next;

    TransactionsRecord() {
      super(null, null);
      stamp = 0L;
      writeFilter = new long[64];
      writeFilterSummary = 0L;
      status = Status.FINALIZED;
    }

    TransactionsRecord(final TransactionsRecord prev,
                       final ReferenceQueue<TransactionsRecord> queue,
                       final long stamp,
                       final long[] writeFilter,
                       final long writeFilterSummary) {
      super(prev, queue);
      this.stamp = stamp;
      this.writeFilter = writeFilter;
      this.writeFilterSummary = writeFilterSummary;
    }

    enum Status {
      VALID, COMMITTED, FINALIZED
    }
  }

  public static final class Var implements TruffleObject {
    final TransactionalMemory parent;
    final long id;
    Box current;

    public Var(TransactionalMemory stm, final Object initial) {
      parent = stm;
      id = (((long) identityHashCode(this)) << 32) | (currentTimeMillis() & 0xffffffffL);
      current = new Var.Box(initial);
      stm.registerVar(new WeakReference<>(this, stm.varsQueue));
    }

    public Object read() {
      return current.value;
    }

    public Object read(final Transaction transaction, final Node node) {
      if (transaction.parent() != parent) {
        throw new YattaException("Transactional variable belongs to a different transactional memory instance.", node);
      }
      transaction.registerRead(this);
      return fetchValue(transaction);
    }

    Object fetchValue(final Transaction transaction) {
      Object result = transaction.lookupWrites(this);
      if (result == null) {
        final long stamp = transaction.snapshotStamp();
        Var.Box cursor = current;
        while (cursor.stamp > stamp) {
          cursor = cursor.prev;
        }
        return cursor.value;
      }
      return result;
    }

    public void protect(final Transaction transaction, final Node node) {
      if (transaction.parent() != parent) {
        throw new YattaException("Transactional variable belongs to a different transactional memory instance.", node);
      }
      transaction.registerProtect(this, node);
    }

    public void write(final Transaction transaction, final Object value, final Node node) {
      if (transaction.parent() != parent) {
        throw new YattaException("Transactional variable belongs to a different transactional memory instance.", node);
      }
      transaction.registerWrite(this, value, node);
    }

    final class Box {
      Box prev;
      long stamp = -1L;
      final Object value;

      Box(final Object initial) {
        stamp = 0L;
        value = initial;
      }

      Box(final Box prev, final Object value) {
        this.prev = prev;
        this.value = value;
      }

      Var parent() {
        return Var.this;
      }
    }
  }

  public static abstract class Transaction {
    abstract TransactionalMemory parent();

    abstract long snapshotStamp();

    abstract void registerRead(Var var);

    abstract void registerProtect(Var var, Node node);

    abstract void registerWrite(Var var, Object value, Node node);

    abstract Object lookupWrites(Var var);

    public abstract void start();

    public abstract boolean validate();

    public abstract void commit();

    public abstract void abort();

    public abstract void reset();
  }

  public static final class ReadOnlyTransaction extends Transaction {
    final TransactionalMemory stm;
    TransactionsRecord activeRecord;

    public ReadOnlyTransaction(final TransactionalMemory parent) {
      stm = parent;
    }

    @Override
    TransactionalMemory parent() {
      return stm;
    }

    @Override
    long snapshotStamp() {
      return activeRecord.stamp;
    }

    @Override
    void registerRead(final Var var) { }

    @Override
    void registerProtect(final Var var, final Node node) {
      throw new YattaException("Can't protect in read-only transaction", node);
    }

    @Override
    void registerWrite(final Var var, final Object value, final Node node) {
      throw new YattaException("Can't write in read-only transaction", node);
    }

    @Override
    Object lookupWrites(final Var var) {
      return null;
    }

    @Override
    public void start() {
      activeRecord = stm.lastCommittedRecord;
    }

    @Override
    public boolean validate() {
      return true;
    }

    @Override
    public void commit() {
      activeRecord = null;
    }

    @Override
    public void abort() {
      activeRecord = null;
    }

    @Override
    public void reset() {
      activeRecord = null;
    }
  }

  public static final class ReadWriteTransaction extends Transaction {
    static final VarHandle READ_FILTER_HANDLE = MethodHandles.arrayElementVarHandle(long[].class);
    static final VarHandle READ_FILTER_SUMMARY_HANDLE;

    static {
      try {
        READ_FILTER_SUMMARY_HANDLE = MethodHandles.lookup().findVarHandle(ReadWriteTransaction.class, "readFilterSummary", long.class);
      } catch (NoSuchFieldException | IllegalAccessException e) {
        throw new AssertionError(e);
      }
    }

    final TransactionalMemory stm;
    TransactionsRecord activeRecord;
    final long[] readFilter = new long[64];
    volatile long readFilterSummary;
    final long[] writeFilter = new long[64];
    long writeFilterSummary;
    Var.Box[] writes = new Var.Box[WRITE_SET_INITIAL_CAPACITY];
    TransactionsRecord commitRecord;

    public ReadWriteTransaction(final TransactionalMemory parent) {
      stm = parent;
    }

    @Override
    TransactionalMemory parent() {
      return stm;
    }

    @Override
    long snapshotStamp() {
      return activeRecord.stamp;
    }

    @Override
    void registerRead(final Var var) {
      final long summaryDiff = filtersWrite(readFilter, BLOOM_FILTER_HASHES, var.id, READ_FILTER_HANDLE);
      long expectedSummary;
      long updatedSummary;
      do {
        expectedSummary = readFilterSummary;
        updatedSummary = expectedSummary | summaryDiff;
      } while (!READ_FILTER_SUMMARY_HANDLE.compareAndSet(this, expectedSummary, updatedSummary));
    }

    @Override
    void registerProtect(Var var, Node node) {
      final long summaryDiff = filtersWrite(writeFilter, BLOOM_FILTER_HASHES, var.id);
      writeFilterSummary |= summaryDiff;
      filtersWrite(readFilter, BLOOM_FILTER_HASHES, var.id, READ_FILTER_HANDLE);
      long expectedSummary;
      long updatedSummary;
      do {
        expectedSummary = readFilterSummary;
        updatedSummary = expectedSummary | summaryDiff;
      } while (!READ_FILTER_SUMMARY_HANDLE.compareAndSet(this, expectedSummary, updatedSummary));
    }

    @Override
    void registerWrite(Var var, Object value, Node node) {
      final long summaryDiff = filtersWrite(writeFilter, BLOOM_FILTER_HASHES, var.id);
      writeFilterSummary |= summaryDiff;
      writesAdd(writes, var.new Box(var.current, value), WRITE_SET_PROBES);
    }

    @Override
    Object lookupWrites(Var var) {
      return writesLookup(writes, var, WRITE_SET_PROBES);
    }

    @Override
    public void start() {
      activeRecord = stm.lastCommittedRecord;
    }

    @Override
    public boolean validate() {
      if (writeFilterSummary != 0L) {
        TransactionsRecord lastValid = activeRecord;
        do {
          TransactionsRecord next = lastValid.next;
          while (next != null) {
            lastValid = next;
            if (filterSummariesMightIntersect(readFilterSummary, lastValid.writeFilterSummary)) {
              if (filtersMightIntersect(readFilter, lastValid.writeFilter, BLOOM_FILTER_HASHES)) {
                return false;
              }
            }
            next = next.get();
          }
          commitRecord = new TransactionsRecord(lastValid, parent().recordsQueue, lastValid.stamp + 1, writeFilter, writeFilterSummary);
        } while (!TransactionsRecord.NEXT_HANDLE.compareAndSet(lastValid, null, commitRecord));
      }
      return true;
    }

    @Override
    public void commit() {
      if (writeFilterSummary != 0L) {
        TransactionsRecord current = activeRecord;
        while (current != commitRecord) {
          if (current.status == TransactionsRecord.Status.VALID) {
            if (filterSummariesMightIntersect(writeFilterSummary, current.writeFilterSummary)) {
              if (filtersMightIntersect(writeFilter, current.writeFilter, BLOOM_FILTER_HASHES)) {
                Thread.yield();
                continue;
              }
            }
          }
          current = current.next;
        }
        for (Var.Box box : writes) {
          if (box != null) {
            box.stamp = commitRecord.stamp;
            box.parent().current = box;
          }
        }
        commitRecord.status = TransactionsRecord.Status.COMMITTED;
        current = activeRecord;
        while (current != commitRecord) {
          if (current.status == TransactionsRecord.Status.FINALIZED) {
            current = current.next;
          } else {
            Thread.onSpinWait();
          }
        }
        stm.lastCommittedRecord = commitRecord;
        commitRecord.status = TransactionsRecord.Status.FINALIZED;
      }
      activeRecord = null;
      commitRecord = null;
    }

    @Override
    public void abort() {
      activeRecord = null;
    }

    @Override
    public void reset() {
      activeRecord = null;
      fill(readFilter, 0L);
      readFilterSummary = 0L;
      fill(writeFilter, 0L);
      writeFilterSummary = 0L;
      fill(writes, null);
    }
  }

  // writes hash into 4096-bit filter, returning 64-bit summary diff
  static long filtersWrite(final long[] filter, final int hashes, final long hash) {
    long result = 0L;
    int mix;
    long bit;
    long mask;
    int idx;
    for (int i = 1; i <= hashes; i++) {
      mix = filtersMix(hash, i);
      bit = mix % 4096;
      mask = 1L << bit;
      idx = (int) (bit >>> 6);
      filter[idx] |= mask;
      result |= (1L << idx);
    }
    return result;
  }

  private static int filtersMix(final long hash, final int i) {
    final int mixed = (int) hash + (i * (int) (hash >>> 32));
    return mixed >= 0 ? mixed : ~mixed;
  }

  static long filtersWrite(final long[] filter, final int hashes, final long hash, final VarHandle handle) {
    long result = 0L;
    int mix;
    long bit;
    long mask;
    int idx;
    for (int i = 1; i <= hashes; i++) {
      mix = filtersMix(hash, i);
      bit = mix % 4096;
      mask = 1L << bit;
      idx = (int) (bit >>> 6);
      long expected;
      long updated;
      do {
        expected = (long) handle.getVolatile(filter, idx);
        updated = expected | mask;
      } while (!handle.compareAndSet(filter, idx, expected, updated));
      result |= (1L << idx);
    }
    return result;
  }

  static boolean filtersMightIntersect(final long[] first, final long[] second, final int hashes) {
    int intersections = 0;
    for (int i = 0; i < 64; i++) {
      intersections += Long.bitCount(first[i] & second[i]);
      if (intersections >= hashes) {
        return true;
      }
    }
    return false;
  }

  static boolean filterSummariesMightIntersect(final long first, final long second) {
    return (first & second) != 0L;
  }

  // write set length has to be power of two, probes has to be positive
  static Var.Box[] writesAdd(final Var.Box[] writes, final Var.Box value, final int probes) {
    final int idx = writesIndex(value.parent().id, writes.length);
    int probesLeft = probes;
    for (int i = idx; i < writes.length; i++) {
      final Var.Box member = writes[i];
      if (member == null || member.parent() == value.parent()) {
        writes[i] = value;
        return writes;
      } else {
        if (--probesLeft == 0) {
          break;
        }
      }
    }
    return writesAdd(writesGrow(writes), value, probes);
  }

  static int writesIndex(final long hash, final int len) {
    return (int) (hash & (len - 1));
  }

  static Var.Box[] writesGrow(final Var.Box[] writes) {
    final Var.Box[] result = new Var.Box[writes.length * 2];
    for (Var.Box member : writes) {
      if (member != null) {
        result[writesIndex(member.parent().id, result.length)] = member;
      }
    }
    return result;
  }

  static Object writesLookup(final Var.Box[] writes, final Var var, int probes) {
    final int idx = writesIndex(var.id, writes.length);
    for (int i = idx; i < writes.length; i++) {
      final Var.Box member = writes[i];
      if (member == null) {
        break;
      } else if (member.parent() == var) {
        return member.value;
      } else {
        if (probes-- == 0) {
          break;
        }
      }
    }
    return null;
  }
}
