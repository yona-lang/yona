package yatta.runtime.async;

import yatta.runtime.Dict;
import yatta.runtime.Hasher;
import yatta.runtime.Set;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;
import java.lang.ref.PhantomReference;
import java.lang.ref.ReferenceQueue;
import java.lang.ref.WeakReference;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

public final class STM {
  static final Dict EMPTY_WRITES = Dict.empty(Id.INSTANCE, 0L);

  volatile TransactionsRecord lastCommittedRecord = new TransactionsRecord();
  final ReferenceQueue<TransactionsRecord> gcQueue = new ReferenceQueue<>();
  final AtomicReference<Set> vars = new AtomicReference<>(Set.empty());

  public Transaction newTransaction(boolean readOnly) {
    return readOnly ? new ReadOnlyTransaction(this) : new TopLevelReadWriteTransaction(this);
  }

  @SuppressWarnings("unchecked")
  void gc() {
    TransactionsRecord record = (TransactionsRecord) gcQueue.poll();
    if (record != null) {
      final long stamp = record.prevStamp;
      vars.get().forEach(ref -> {
        Var var = ((WeakReference<Var>) ref).get();
        if (var != null) {
          StampedBox top = var.box;
          StampedBox bottom = top.prev;
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
      });
      record.clear();
    }
  }

  static long summarize(final long[] filter) {
    long result = 0L;
    for (int i = 0; i < 64; i++) {
      if (filter[i] != 0) {
        result |= (1L << i);
      }
    }
    return result;
  }

  static boolean summariesMightIntersect(final long fst, final long snd) {
    return (fst & snd) != 0L;
  }

  public final class Var {
    final long id;
    StampedBox box;

    public Var(final Object initial) {
      id = (((long) System.identityHashCode(this)) << 32) | (System.currentTimeMillis() & 0xffffffffL);
      box = new StampedBox(null, 0L, initial);
      Set expectedVars;
      Set updatedVars;
      do {
        expectedVars = STM.this.vars.get();
        updatedVars = expectedVars.add(new WeakReference<>(this));
      } while (!STM.this.vars.compareAndSet(expectedVars, updatedVars));
    }

    public Object read() {
      return box.value;
    }

    public Object read(final Transaction transaction) {
      transaction.read(this);
      if (transaction.writesContains(this)) {
        return transaction.writesLookup(this);
      } else {
        final long stamp = transaction.activeRecord.stamp;
        StampedBox cursor = box;
        while (cursor.stamp > stamp) {
          cursor = cursor.prev;
        }
        return cursor.value;
      }
    }

    public void ensure(final Transaction transaction) {
      transaction.ensure(this);
    }

    public void write(final Object value, final Transaction transaction) {
      transaction.write(this, value);
    }
  }

  static final class StampedBox {
    final long stamp;
    final Object value;
    StampedBox prev;

    StampedBox(final StampedBox prev, final long stamp, final Object value) {
      this.stamp = stamp;
      this.value = value;
      this.prev = prev;
    }
  }

  static final class TransactionsRecord extends PhantomReference<TransactionsRecord> {
    final AtomicReference<TransactionsRecord> next = new AtomicReference<>();
    final long stamp;
    final long writeFilterSummary;
    final long[] writeFilter;
    final Dict writes;
    volatile Status status;
    long prevStamp;

    TransactionsRecord() {
      super(null, null);
      stamp = 0L;
      writeFilterSummary = 0L;
      writeFilter = new long[64];
      writes = EMPTY_WRITES;
      status = Status.COMMITTED;
    }

    TransactionsRecord(final long stamp, final long writeFilterSummary, final long[] writeFilter, final Dict writes, final TransactionsRecord prev, final ReferenceQueue<TransactionsRecord> queue) {
      super(prev, queue);
      this.stamp = stamp;
      this.writeFilterSummary = writeFilterSummary;
      this.writeFilter = writeFilter;
      this.writes = writes;
      this.status = Status.VALID;
      this.prevStamp = prev.stamp;
    }
  }

  enum Status {
    VALID, WRITTEN_BACK, COMMITTED
  }

  static abstract class Transaction {
    final TransactionsRecord activeRecord;

    protected Transaction(final TransactionsRecord activeRecord) {
      this.activeRecord = activeRecord;
    }

    abstract void read(Var var);

    abstract void ensure(Var var);

    abstract void write(Var var, Object value);

    abstract boolean writesContains(Var var);

    abstract Object writesLookup(Var var);

    public abstract boolean validate();

    public abstract void commit();

    public abstract void abort();
  }

  static final class ReadOnlyTransaction extends Transaction {
    ReadOnlyTransaction(final STM stm) {
      super(stm.lastCommittedRecord);
    }

    @Override
    void read(final Var var) {}

    @Override
    void ensure(final Var var) {
      throw new UnsupportedOperationException();
    }

    @Override
    void write(final Var var, final Object value) {
      throw new UnsupportedOperationException();
    }

    @Override
    boolean writesContains(final Var var) {
      return false;
    }

    @Override
    Object writesLookup(final Var var) {
      throw new AssertionError();
    }

    @Override
    public boolean validate() {
      return true;
    }

    @Override
    public void commit() { }

    @Override
    public void abort() {}
  }

  static abstract class ReadWriteTransaction extends Transaction {
    static final VarHandle READ_FILTER_HANDLE;
    static final VarHandle READ_FILTER_SUMMARY_HANDLE;

    static {
      READ_FILTER_HANDLE = MethodHandles.arrayElementVarHandle(long[].class);
      try {
        READ_FILTER_SUMMARY_HANDLE = MethodHandles.lookup().findVarHandle(ReadWriteTransaction.class, "readFilterSummary", long.class);
      } catch (final Exception e) {
        throw new AssertionError(e);
      }
    }

    final long[] readFilter = new long[64];
    final long[] writeFilter = new long[64];
    long readFilterSummary;
    long writeFilterSummary;
    Dict writes = EMPTY_WRITES;

    ReadWriteTransaction(final TransactionsRecord activeRecord) {
      super(activeRecord);
    }

    @Override
    final void read(final Var var) {
      BloomFilter.add(readFilter, 7, Id.INSTANCE.hash(0L, var), READ_FILTER_HANDLE);
      long expectedSummary;
      long updatedSummary;
      do {
        expectedSummary = readFilterSummary;
        updatedSummary = summarize(readFilter);
      } while (!READ_FILTER_SUMMARY_HANDLE.compareAndSet(this, expectedSummary, updatedSummary));
    }

    @Override
    final void ensure(final Var var) {
      BloomFilter.add(readFilter, 7, Id.INSTANCE.hash(0L, var), READ_FILTER_HANDLE);
      long expectedSummary;
      long updatedSummary;
      do {
        expectedSummary = readFilterSummary;
        updatedSummary = summarize(readFilter);
      } while (!READ_FILTER_SUMMARY_HANDLE.compareAndSet(this, expectedSummary, updatedSummary));
      BloomFilter.add(writeFilter, 7, Id.INSTANCE.hash(0L, var));
      writeFilterSummary = summarize(writeFilter);
    }

    @Override
    final void write(final Var var, final Object value) {
      BloomFilter.add(writeFilter, 7, Id.INSTANCE.hash(0L, var));
      writeFilterSummary = summarize(writeFilter);
      writes = writes.add(var, value);
    }
  }

  static final class TopLevelReadWriteTransaction extends ReadWriteTransaction {
    final STM stm;
    TransactionsRecord commitRecord;

    TopLevelReadWriteTransaction(final STM stm) {
      super(stm.lastCommittedRecord);
      this.stm = stm;
    }

    @Override
    boolean writesContains(final Var var) {
      return writes.contains(var);
    }

    @Override
    Object writesLookup(final Var var) {
      return writes.lookup(var);
    }

    @Override
    public boolean validate() {
      if (writes == EMPTY_WRITES) {
        return true;
      }
      TransactionsRecord lastValid = activeRecord;
      do {
        TransactionsRecord next = lastValid.next.get();
        while (next != null) {
          lastValid = next;
          if (summariesMightIntersect(readFilterSummary, lastValid.writeFilterSummary)) {
            if (BloomFilter.mightIntersect(readFilter, lastValid.writeFilter, 7)) {
              return false;
            }
          }
          next = lastValid.next.get();
        }
        commitRecord = new TransactionsRecord(lastValid.stamp + 1, writeFilterSummary, writeFilter, writes, lastValid, stm.gcQueue);
      } while (!lastValid.next.compareAndSet(null, commitRecord));
      return true;
    }

    @Override
    public void commit() {
      if (writes == EMPTY_WRITES) {
        return;
      }
      // incrementally check if there might be an intersection or we can write already
      TransactionsRecord current = activeRecord;
      while (current != commitRecord) {
        if (current.status != Status.VALID) {
          current = current.next.get();
        } else {
          if (!summariesMightIntersect(writeFilterSummary, current.writeFilterSummary) || !BloomFilter.mightIntersect(writeFilter, current.writeFilter, 7)) {
            current = current.next.get();
          } else {
            Thread.yield();
          }
        }
      }
      // write-back
      writes.forEach((k, v) -> {
        final Var var = (Var) k;
        var.box = new StampedBox(var.box, activeRecord.stamp, v);
      });
      commitRecord.status = Status.WRITTEN_BACK;
      // loop until all committing transactions before us are done
      current = activeRecord;
      while (current != commitRecord) {
        if (current.status == Status.COMMITTED) {
          current = current.next.get();
        } else {
          Thread.yield();
        }
      }
      // finalize
      stm.lastCommittedRecord = commitRecord;
      commitRecord.status = Status.COMMITTED;
      stm.gc();
    }

    @Override
    public void abort() {}
  }

  static final class Id extends Hasher {
    static final Hasher INSTANCE = new Id();

    @Override
    public long hash(final long seed, final Object var) {
      return seed ^ ((Var) var).id;
    }
  }
}
