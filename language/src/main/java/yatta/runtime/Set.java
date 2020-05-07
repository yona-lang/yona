package yatta.runtime;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.interop.*;
import com.oracle.truffle.api.library.ExportLibrary;
import com.oracle.truffle.api.library.ExportMessage;
import com.oracle.truffle.api.nodes.Node;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.TransducerDoneException;

import java.lang.reflect.Array;
import java.util.function.BiFunction;
import java.util.function.Consumer;

@ExportLibrary(InteropLibrary.class)
public abstract class Set implements TruffleObject, Comparable<Set> {
  static final int BITS = 6;
  static final int MASK = (1 << BITS) - 1;

  static final Object[] EMPTY_ARRAY = new Object[]{};

  final Hasher hasher;
  final long seed;

  volatile long hash = 0L;

  Set(final Hasher hasher, final long seed) {
    this.hasher = hasher;
    this.seed = seed;
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public static Set set(Object... args) {
    Set set = Set.empty(Murmur3.INSTANCE, 0L);
    for (Object arg : args) {
      set = set.add(arg);
    }

    return set;
  }

  @CompilerDirectives.TruffleBoundary
  public Object[] toArray() {
    return toArray(Object.class);
  }

  @CompilerDirectives.TruffleBoundary
  public <T> T[] toArray(Class<T> cls) {
    assert size() < Integer.MAX_VALUE;
    T[] res = (T[]) Array.newInstance(cls, (int) size());
    fold(0, (acc, el) -> {
      res[acc] = (T) el;
      return acc + 1;
    });
    return res;
  }

  @CompilerDirectives.TruffleBoundary
  public Object unwrapPromises(final Node node) {
    boolean hasPromise = fold(false, (acc, el) -> {
      if (el instanceof Promise) {
        return true;
      } else {
        return acc;
      }
    });

    if (!hasPromise) {
      return this;
    } else {
      return Promise.all(toArray(), node);
    }
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public final Set add(final Object value) {
    return add(value, hasher.hash(seed, value), 0);
  }

  abstract Set add(Object value, long hash, int shift);

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public final boolean contains(final Object value) {
    return contains(value, hasher.hash(seed, value), 0);
  }

  abstract boolean contains(Object value, long hash, int shift);

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public final Set remove(final Object value) {
    return remove(value, hasher.hash(seed, value), 0);
  }

  abstract Set remove(Object value, long hash, int shift);

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public abstract Object reduce(Object[] reducer, InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException;

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public abstract Object fold(Object initial, Function function, InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException;

  public abstract <T> T fold(final T initial, final BiFunction<T, Object, T> function);

  public abstract void forEach(final Consumer<? super Object> consumer);

  @ExportMessage
  public boolean isString() {
    return true;
  }

  @Override
  public int compareTo(Set o) {
    int ret = fold(0, (acc, el) -> {
      boolean containedInOther = o.contains(el);
      if (containedInOther && acc == 0) {
        return 0;
      } else if (containedInOther && acc < 0) {
        return -1;
      } else {
        return 1;
      }
    });
    if (ret == 0 && size() != o.size()) {
      return -1;
    } else {
      return ret;
    }
  }

  @CompilerDirectives.TruffleBoundary
  public Set union(Set other) {
    return other.fold(this, Set::add);
  }

  @CompilerDirectives.TruffleBoundary
  public Set intersection(Set other) {
    return other.fold(Set.set(), (acc, el) -> {
      if(contains(el) && other.contains(el)) {
        return acc.add(el);
      } else {
        return acc;
      }
    });
  }

  @CompilerDirectives.TruffleBoundary
  public Set symmetricDifference(Set other) {
    return union(other).fold(Set.set(), (acc, el) -> {
      if((contains(el) && !other.contains(el)) || (!contains(el) && other.contains(el))) {
        return acc.add(el);
      } else {
        return acc;
      }
    });
  }

  @Override
  @CompilerDirectives.TruffleBoundary
  public String toString() {
    StringBuilder sb = new StringBuilder();
    sb.append("{");
    fold(sb, (res, el) -> res.append(el).append(", "));
    if(size() > 0) {
      sb.deleteCharAt(sb.length() - 1);
      sb.deleteCharAt(sb.length() - 1);
    }
    sb.append("}");
    return sb.toString();
  }

  @ExportMessage
  @CompilerDirectives.TruffleBoundary
  public String asString() {
    return toString();
  }

  static boolean isInstance(TruffleObject set) {
    return set instanceof Set;
  }

  public abstract long size();

  final long murmur3Hash(long seed) {
    if (seed == 0L) {
      if (hash == 0L) {
        hash = calculateMurmur3Hash(0L);
      }
      return hash;
    } else {
      return calculateMurmur3Hash(seed);
    }
  }

  abstract long calculateMurmur3Hash(long seed);

  @Override
  public final int hashCode() {
    return (int) murmur3Hash(0);
  }

  final Set merge(final Object fst, final long fstHash, final Object snd, final long sndHash, final int shift) {
    if (shift > (1 << BITS)) {
      return new Collision(hasher, seed, fstHash, new Object[]{ fst, snd });
    }
    final long fstMask = mask(fstHash, shift);
    final long sndMask = mask(sndHash, shift);
    if (fstMask < sndMask) {
      return new Bitmap(hasher, seed, 0, pos(fstMask) | pos(sndMask), new Object[]{ fst, snd });
    } else if (fstMask > sndMask) {
      return new Bitmap(hasher, seed, 0, pos(fstMask) | pos(sndMask), new Object[]{ snd, fst });
    } else {
      return new Bitmap(hasher, seed, pos(fstMask), 0, new Object[]{ merge(fst, fstHash, snd, sndHash, shift + BITS) });
    }
  }

  abstract Object dataAt(final int idx);

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public static Set empty(final Hasher hasher, final long seed) {
    return new Bitmap(hasher, seed, 0L, 0L, EMPTY_ARRAY);
  }

  public static Set empty() {
    return empty(Murmur3.INSTANCE, 0L);
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public static Set singleton(final Hasher hasher, final long seed, final Object value) {
    return new Bitmap(hasher, seed, 0L, pos(mask(hasher.hash(seed, value), 0)), new Object[]{ value });
  }

  static long mask(final long hash, final int shift) {
    return (hash >>> shift) & MASK;
  }

  static long pos(final long mask) {
    return 1L << mask;
  }

  static final class Bitmap extends Set {
    final long nodeBmp;
    final long dataBmp;
    final Object[] elements;

    Bitmap(final Hasher hasher, final long seed, long nodeBmp, long dataBmp, Object[] elements) {
      super(hasher, seed);
      this.nodeBmp = nodeBmp;
      this.dataBmp = dataBmp;
      this.elements = elements;
    }

    @Override
    Set add(final Object value, final long hash, final int shift) {
      final long mask = mask(hash, shift);
      final long pos = pos(mask);
      if ((dataBmp & pos) != 0) {
        final int dataIdx = index(pos, dataBmp);
        final Object currentValue = dataAt(dataIdx);
        return value.equals(currentValue) ? this : promote(pos, merge(currentValue, hasher.hash(seed, currentValue), value, hash, shift + BITS));
      } else if ((nodeBmp & pos) != 0) {
        final Set oldSub = nodeAt(index(pos, nodeBmp));
        final Set newSub = oldSub.add(value, hash, shift + BITS);
        return newSub == oldSub ? this : replaceNode(pos, newSub);
      } else {
        return insertValue(pos, value);
      }
    }

    Set promote(final long pos, final Set node) {
      final int oldIdx = index(pos, dataBmp);
      final int newIdx = elements.length - 1 - index(pos, nodeBmp);
      final Object[] newElements = new Object[elements.length];
      System.arraycopy(elements, 0, newElements, 0, oldIdx);
      System.arraycopy(elements, oldIdx + 1, newElements, oldIdx, newIdx - oldIdx);
      newElements[newIdx] = node;
      System.arraycopy(elements, newIdx + 1, newElements, newIdx + 1, elements.length - newIdx - 1);
      return new Bitmap(hasher, seed, nodeBmp | pos, dataBmp ^ pos, newElements);
    }

    Set nodeAt(final int idx) {
      return (Set) elements[elements.length - 1 - idx];
    }

    Set replaceNode(final long pos, final Set node) {
      final Object[] newElements = elements.clone();
      newElements[elements.length - 1 - index(pos, nodeBmp)] = node;
      return new Bitmap(hasher, seed, nodeBmp, dataBmp, newElements);
    }

    Set insertValue(final long pos, final Object value) {
      final int idx = index(pos, dataBmp);
      final Object[] newElements = new Object[elements.length + 1];
      System.arraycopy(elements, 0, newElements, 0, idx);
      newElements[idx] = value;
      System.arraycopy(elements, idx, newElements, idx + 1, elements.length - idx);
      return new Bitmap(hasher, seed, nodeBmp, dataBmp | pos, newElements);
    }

    @Override
    boolean contains(final Object value, final long hash, final int shift) {
      final long mask = mask(hash, shift);
      final long pos = pos(mask);
      if ((dataBmp & pos) != 0) {
        final int index = index(pos, dataBmp, mask);
        return value.equals(dataAt(index));
      } else if ((nodeBmp & pos) != 0) {
        final int index = index(pos, nodeBmp, mask);
        return nodeAt(index).contains(value, hash, shift + BITS);
      } else return false;
    }

    static int index(final long pos, final long bitmap, final long mask) {
      return (bitmap == -1L) ? (int) mask : index(pos, bitmap);
    }

    @Override
    Set remove(final Object value, final long hash, final int shift) {
      final long mask = mask(hash, shift);
      final long pos = pos(mask);
      if ((dataBmp & pos) != 0) {
        final int dataIndex = index(pos, dataBmp);
        if (value.equals(dataAt(dataIndex))) {
          if (Long.bitCount(dataBmp) == 2 && Long.bitCount(nodeBmp) == 0) {
            final long newDataBmp = (shift == 0) ? dataBmp ^ pos : pos(mask(hash, 0));
            return dataIndex == 0 ? new Bitmap(hasher, seed, 0L, newDataBmp, new Object[]{ dataAt(1) }) : new Bitmap(hasher, seed, 0L, newDataBmp, new Object[]{ dataAt(0) });
          } else {
            return removeValue(pos);
          }
        } else {
          return this;
        }
      } else if ((nodeBmp & pos) != 0) {
        final Set oldSub = nodeAt(index(pos, nodeBmp));
        final Set newSub = oldSub.remove(value, hash, shift + BITS);
        if (newSub == oldSub) {
          return this;
        }
        if (newSub instanceof Bitmap && arity(((Bitmap) newSub).dataBmp) == 1 && arity(((Bitmap) newSub).nodeBmp) == 0) {
          if (arity(dataBmp) == 0 && arity(nodeBmp) == 1) {
            return newSub;
          } else {
            return demote(pos, newSub);
          }
        } else {
          return replaceNode(pos, newSub);
        }
      }
      return this;
    }

    Set removeValue(final long pos) {
      final int idx = index(pos, dataBmp);
      final Object[] newElements = new Object[elements.length - 1];
      System.arraycopy(elements, 0, newElements, 0, idx);
      System.arraycopy(elements, idx + 1, newElements, idx, elements.length - idx - 1);
      return new Bitmap(hasher, seed, nodeBmp, dataBmp ^ pos, newElements);
    }

    Set demote(final long pos, final Set node) {
      final int oldIdx = elements.length - 1 - index(pos, nodeBmp);
      final int newIdx = index(pos, dataBmp);
      final Object[] newElements = new Object[elements.length];
      System.arraycopy(elements, 0, newElements, 0, newIdx);
      newElements[newIdx] = node.dataAt(0);
      System.arraycopy(elements, newIdx, newElements, newIdx + 1, oldIdx - newIdx);
      System.arraycopy(elements, oldIdx + 1, newElements, oldIdx + 1, elements.length - oldIdx - 1);
      return new Bitmap(hasher, seed, nodeBmp ^ pos, dataBmp | pos, newElements);
    }

    @Override
    public Object reduce(Object[] reducer, InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException {
      final Function step = (Function) reducer[1];
      final Function complete = (Function) reducer[2];
      Object state = reducer[0];
      try {
        for (int i = 0; i < arity(dataBmp); i++) {
          state = dispatch.execute(step, state, dataAt(i));
        }
        for (int i = 0; i < arity(nodeBmp); i++) {
          state = nodeAt(i).fold(state, step, dispatch);
        }
      } catch (TransducerDoneException ignored) {}
      return dispatch.execute(complete, state);
    }

    @Override
    public Object fold(final Object initial, final Function function, final InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException {
      Object result = initial;
      for (int i = 0; i < arity(dataBmp); i++) {
        result = dispatch.execute(function, result, dataAt(i));
      }
      for (int i = 0; i < arity(nodeBmp); i++) {
        result = nodeAt(i).fold(result, function, dispatch);
      }
      return result;
    }

    @Override
    public <T> T fold(final T initial, final BiFunction<T, Object, T> function) {
      T result = initial;
      for (int i = 0; i < arity(dataBmp); i++) {
        result = function.apply(result, dataAt(i));
      }
      for (int i = 0; i < arity(nodeBmp); i++) {
        result = nodeAt(i).fold(result, function);
      }
      return result;
    }

    @Override
    public void forEach(final Consumer<? super Object> consumer) {
      for (int i = 0; i < arity(dataBmp); i++) {
        consumer.accept(dataAt(i));
      }
      for (int i = 0; i < arity(nodeBmp); i++) {
        nodeAt(i).forEach(consumer);
      }
    }

    @Override
    public long size() {
      long result = arity(dataBmp);
      for (int i = 0; i < arity(nodeBmp); i++) {
        result += nodeAt(i).size();
      }
      return result;
    }

    @Override
    long calculateMurmur3Hash(final long seed) {
      long hash = seed;
      for (int i = 0; i < arity(dataBmp); i++) {
        long k = Murmur3.INSTANCE.hash(seed, dataAt(i));
        k *= Murmur3.C1;
        k = Long.rotateLeft(k, 31);
        k *= Murmur3.C2;
        hash ^= k;
        hash = Long.rotateLeft(hash, 27) * 5 + 0x52dce729;
      }
      for (int i = 0; i < arity(nodeBmp); i++) {
        long k = nodeAt(i).murmur3Hash(seed);
        k *= Murmur3.C1;
        k = Long.rotateLeft(k, 31);
        k *= Murmur3.C2;
        hash ^= k;
        hash = Long.rotateLeft(hash, 27) * 5 + 0x52dce729;
      }
      return Murmur3.fMix64(hash ^ size());
    }

    @Override
    @CompilerDirectives.TruffleBoundary(allowInlining = true)
    public boolean equals(Object o) {
      if (o == this) {
        return true;
      }
      if (!(o instanceof Bitmap)) {
        return false;
      }
      final Bitmap that = (Bitmap) o;
      if (this.seed != that.seed) {
        return false;
      }
      if (!this.hasher.equals(that.hasher)) {
        return false;
      }
      if (this.nodeBmp != that.nodeBmp) {
        return false;
      }
      if (this.dataBmp != that.dataBmp) {
        return false;
      }
      if (this.elements.length != that.elements.length) {
        return false;
      }
      for (int i = 0; i < elements.length; i++) {
        if (!this.elements[i].equals(that.elements[i])) {
          return false;
        }
      }
      return true;
    }

    @Override
    Object dataAt(final int idx) {
      return elements[idx];
    }

    static int index(final long pos, final long bitmap) {
      return Long.bitCount(bitmap & (pos - 1));
    }

    static int arity(final long bitmap) {
      return Long.bitCount(bitmap);
    }
  }

  static final class Collision extends Set {
    final long commonHash;
    final Object[] values;

    Collision(final Hasher hasher, final long seed, final long commonHash, final Object[] values) {
      super(hasher, seed);
      this.commonHash = commonHash;
      this.values = values;
    }

    @Override
    Set add(final Object value, final long hash, final int shift) {
      for (Object val : values) {
        if (value.equals(val)) {
          return this;
        }
      }
      final Object[] newValues = new Object[values.length + 1];
      System.arraycopy(values, 0, newValues, 0, values.length);
      newValues[values.length] = value;
      return new Collision(hasher, seed, hash, newValues);
    }

    @Override
    boolean contains(final Object value, final long hash, final int shift) {
      if (hash == this.commonHash) {
        for (Object val : values) {
          if (value.equals(val)) {
            return true;
          }
        }
      }
      return false;
    }

    @Override
    Set remove(final Object value, final long hash, final int shift) {
      for (int idx = 0; idx < values.length; idx++) {
        if (value.equals(values[idx])) {
          if (values.length == 1) {
            return empty(hasher, seed);
          } else {
            final Object[] newValues = new Object[values.length - 1];
            System.arraycopy(values, 0, newValues, 0, idx);
            System.arraycopy(values, idx + 1, newValues, idx, values.length - idx - 1);
            return new Collision(hasher, seed, hash, newValues);
          }
        }
      }
      return this;
    }

    @Override
    public Object reduce(Object[] reducer, InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException {
      final Function step = (Function) reducer[1];
      final Function complete = (Function) reducer[2];
      Object state = reducer[0];
      try {
        for (Object val : values) {
          state = dispatch.execute(step, state, val);
        }
      } catch (TransducerDoneException ignored) {}
      return dispatch.execute(complete, state);
    }

    @Override
    public Object fold(final Object initial, final Function function, final InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException {
      Object result = initial;
      for (Object val : values) {
        result = dispatch.execute(function, result, val);
      }
      return result;
    }

    @Override
    public <T> T fold(final T initial, final BiFunction<T, Object, T> function) {
      T result = initial;
      for (Object val : values) {
        result = function.apply(result, val);
      }
      return result;
    }

    @Override
    public void forEach(final Consumer<? super Object> consumer) {
      for (Object val : values) {
        consumer.accept(val);
      }
    }

    @Override
    public long size() {
      return values.length;
    }

    @Override
    long calculateMurmur3Hash(final long seed) {
      long hash = seed;
      for (Object value : values) {
        hash ^= Murmur3.INSTANCE.hash(seed, value);
      }
      return Murmur3.fMix64(hash ^ values.length);
    }

    @Override
    @CompilerDirectives.TruffleBoundary(allowInlining = true)
    public boolean equals(Object o) {
      if (o == this) {
        return true;
      }
      if (!(o instanceof Collision)) {
        return false;
      }
      final Collision that = (Collision) o;
      if (this.seed != that.seed) {
        return false;
      }
      if (!this.hasher.equals(that.hasher)) {
        return false;
      }
      if (this.commonHash != that.commonHash) {
        return false;
      }
      if (this.values.length != that.values.length) {
        return false;
      }
      for (Object value : values) {
        if (!that.contains(value, commonHash, 0)) {
          return false;
        }
      }
      return true;
    }

    @Override
    Object dataAt(final int idx) {
      return values[idx];
    }
  }
}
