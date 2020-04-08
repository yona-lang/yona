package yatta.runtime;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.interop.*;
import com.oracle.truffle.api.library.ExportLibrary;
import com.oracle.truffle.api.library.ExportMessage;
import com.oracle.truffle.api.nodes.Node;
import yatta.common.TriFunction;
import yatta.runtime.async.Promise;
import yatta.runtime.exceptions.BadArgException;
import yatta.runtime.exceptions.TransducerDoneException;

import java.util.Arrays;
import java.util.function.BiConsumer;

@ExportLibrary(InteropLibrary.class)
public abstract class Dict implements TruffleObject, Comparable<Dict> {
  static final int BITS = 6;
  static final int MASK = (1 << BITS) - 1;

  static final Object[] EMPTY_ARRAY = new Object[]{};

  final Hasher hasher;
  final long seed;

  volatile long hash = 0L;

  Dict(final Hasher hasher, final long seed) {
    this.hasher = hasher;
    this.seed = seed;
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public final Dict add(final Object key, final Object value) {
    return add(key, hasher.hash(seed, key), value, 0);
  }

  abstract Dict add(final Object key, final long hash, final Object value, final int shift);

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public final Object lookup(final Object key) {
    final Object result = lookup(key, hasher.hash(seed, key), 0);
    return result == null ? Unit.INSTANCE : result;
  }

  abstract Object lookup(final Object key, final long hash, final int shift);

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public final Dict remove(final Object key) {
    return remove(key, hasher.hash(seed, key), 0);
  }

  abstract Dict remove(Object key, long hash, int shift);

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public abstract Object reduce(Object[] reducer, InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException;

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public abstract Object fold(Object initial, Function function, InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException;

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public abstract <T> T fold(final T initial, final TriFunction<T, Object, Object, T> function);

  public abstract void forEach(final BiConsumer<? super Object, ? super Object> consumer);

  public abstract long size();

  public abstract Set keys();

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

  abstract Object keyAt(int idx);

  abstract Object valueAt(int idx);

  @Override
  public final int hashCode() {
    return (int) murmur3Hash(0);
  }

  final Dict merge(final Object fstKey, final long fstHash, final Object fstValue, final Object sndKey, final long sndHash, final Object sndValue, final int shift) {
    if (shift > (1 << BITS)) {
      return new Collision(hasher, seed, fstHash, new Object[]{fstKey, fstValue, sndKey, sndValue});
    }
    final long fstMask = mask(fstHash, shift);
    final long sndMask = mask(sndHash, shift);
    if (fstMask < sndMask) {
      return new Bitmap(hasher, seed, 0, pos(fstMask) | pos(sndMask), new Object[]{fstKey, fstValue, sndKey, sndValue});
    } else if (fstMask > sndMask) {
      return new Bitmap(hasher, seed, 0, pos(fstMask) | pos(sndMask), new Object[]{sndKey, sndValue, fstKey, fstValue});
    } else {
      return new Bitmap(hasher, seed, pos(fstMask), 0, new Object[]{merge(fstKey, fstHash, fstValue, sndKey, sndHash, sndValue, shift + BITS)});
    }
  }

  @ExportMessage
  public boolean isString() {
    return true;
  }

  @ExportMessage
  @CompilerDirectives.TruffleBoundary
  public String asString() {
    return toString();
  }

  static boolean isInstance(TruffleObject dict) {
    return dict instanceof Dict;
  }

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public static Dict empty(final Hasher hasher, final long seed) {
    return new Bitmap(hasher, seed, 0L, 0L, EMPTY_ARRAY);
  }

  public static Dict empty() {
    return empty(Murmur3.INSTANCE, 0L);
  }

  @Override
  public String toString() {
    StringBuilder sb = new StringBuilder();
    sb.append("{");
    fold(sb, (acc, key, val) -> {
      acc.append(key);
      acc.append(" = ");
      acc.append(val);
      acc.append(", ");
      return acc;
    });
    if (size() > 0) {
      sb.deleteCharAt(sb.length() - 1);
      sb.deleteCharAt(sb.length() - 1);
    }
    sb.append("}");
    return sb.toString();
  }

  public final boolean contains(final Object key) {
    return lookup(key, hasher.hash(seed, key), 0) != null;
  }

  @Override
  public int compareTo(Dict o) {
    int ret = fold(0, (acc, key, val) -> {
      boolean containedInOther = o.contains(key);
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
  public Dict union(Dict other) {
    return other.fold(this, Dict::add);
  }

  @CompilerDirectives.TruffleBoundary
  public Dict intersection(Dict other) {
    return other.fold(Dict.empty(), (acc, key, val) -> {
      if (contains(key) && other.contains(key)) {
        return acc.add(key, val);
      } else {
        return acc;
      }
    });
  }

  @CompilerDirectives.TruffleBoundary
  public Dict symmetricDifference(Dict other) {
    return union(other).fold(Dict.empty(), (acc, key, val) -> {
      if ((contains(key) && !other.contains(key)) || (!contains(key) && other.contains(key))) {
        return acc.add(key, val);
      } else {
        return acc;
      }
    });
  }

  @CompilerDirectives.TruffleBoundary
  public Object unwrapPromises(final Node node) {
    Object[] foldRes = fold(new Object[] {empty(hasher, seed), Seq.EMPTY}, (acc, k, v) -> {
      Dict resultDict = (Dict) acc[0];
      Seq promiseVals = (Seq) acc[1];
      if (k instanceof Promise && v instanceof Promise) {
        if (((Promise) k).isFulfilled() && ((Promise) v).isFulfilled()) {
          resultDict = resultDict.add(((Promise) k).unwrap(), ((Promise) v).unwrap());
        } else {
          promiseVals = promiseVals.insertLast(k);
          promiseVals = promiseVals.insertLast(v);
        }
      } else if (k instanceof Promise) {
        if (((Promise) k).isFulfilled()) {
          resultDict = resultDict.add(((Promise) k).unwrap(), v);
        } else {
          promiseVals = promiseVals.insertLast(k);
          promiseVals = promiseVals.insertLast(v);
        }
      } else if (v instanceof Promise) {
        if (((Promise) v).isFulfilled()) {
          resultDict = resultDict.add(k, ((Promise) v).unwrap());
        } else {
          promiseVals = promiseVals.insertLast(k);
          promiseVals = promiseVals.insertLast(v);
        }
      } else {
        resultDict = resultDict.add(k, v);
      }
      return new Object[] {resultDict, promiseVals};
    });

    Dict resultDict = (Dict) foldRes[0];
    Seq promiseVals = (Seq) foldRes[1];

    if (resultDict.size() == size()) {
      assert promiseVals == Seq.EMPTY;

      return resultDict;
    } else {
      return Promise.all(promiseVals.toArray(), node).map(vals -> resultDict.union(fromArray((Object[]) vals, node, hasher, seed)), node);
    }
  }

  @CompilerDirectives.TruffleBoundary
  private static Dict fromArray(final Object[] args, final Node node, final Hasher hasher, final long seed) {
    if (args.length == 0) {
      return Dict.empty(hasher, seed);
    }
    if (args.length % 2 != 0) {
      throw new BadArgException("Unable to build a dict from array " + Arrays.toString(args), node);
    }
    Dict res = Dict.empty(hasher, seed);
    for (int i = 0; i < args.length; i += 2) {
      res = res.add(args[i], args[i + 1]);
    }

    return res;
  }

  static long mask(final long hash, final int shift) {
    return (hash >>> shift) & MASK;
  }

  static long pos(final long mask) {
    return 1L << mask;
  }

  static final class Bitmap extends Dict {
    final long nodeBmp;
    final long dataBmp;
    final Object[] entriesAndNodes;

    Bitmap(final Hasher hasher, final long seed, final long nodeBmp, final long dataBmp, final Object[] entriesAndNodes) {
      super(hasher, seed);
      this.nodeBmp = nodeBmp;
      this.dataBmp = dataBmp;
      this.entriesAndNodes = entriesAndNodes;
    }

    @Override
    Dict add(final Object key, final long hash, final Object value, final int shift) {
      final long mask = mask(hash, shift);
      final long pos = pos(mask);
      if ((dataBmp & pos) != 0) {
        final int idx = index(pos, dataBmp);
        final Object currentKey = keyAt(idx);
        return key.equals(currentKey) ? replaceValue(idx, value) : promote(pos, merge(currentKey, hasher.hash(seed, currentKey), valueAt(idx), key, hash, value, shift + BITS));
      } else if ((nodeBmp & pos) != 0) {
        final Dict oldSub = nodeAt(index(pos, nodeBmp));
        final Dict newSub = oldSub.add(key, hash, value, shift + BITS);
        return replaceNode(pos, newSub);
      } else {
        return insertKeyAndValue(pos, key, value);
      }
    }

    Dict promote(final long pos, final Dict node) {
      final int oldIdx = index(pos, dataBmp) * 2;
      final int newIdx = entriesAndNodes.length - 1 - index(pos, nodeBmp);
      final Object[] newEntriesAndNodes = new Object[entriesAndNodes.length - 1];
      System.arraycopy(entriesAndNodes, 0, newEntriesAndNodes, 0, oldIdx);
      System.arraycopy(entriesAndNodes, oldIdx + 2, newEntriesAndNodes, oldIdx, newIdx - oldIdx - 1);
      newEntriesAndNodes[newIdx - 1] = node;
      System.arraycopy(entriesAndNodes, newIdx + 1, newEntriesAndNodes, newIdx, entriesAndNodes.length - newIdx - 1);
      return new Bitmap(hasher, seed, nodeBmp | pos, dataBmp ^ pos, newEntriesAndNodes);
    }

    Dict replaceValue(final int idx, final Object value) {
      final Object[] newEntriesAndNodes = entriesAndNodes.clone();
      newEntriesAndNodes[idx * 2 + 1] = value;
      return new Bitmap(hasher, seed, nodeBmp, dataBmp, newEntriesAndNodes);
    }

    Dict replaceNode(final long pos, final Dict node) {
      final Object[] newEntriesAndNodes = entriesAndNodes.clone();
      newEntriesAndNodes[entriesAndNodes.length - 1 - index(pos, nodeBmp)] = node;
      return new Bitmap(hasher, seed, nodeBmp, dataBmp, newEntriesAndNodes);
    }

    Dict insertKeyAndValue(final long pos, final Object key, final Object value) {
      final int idx = index(pos, dataBmp) * 2;
      final Object[] newEntriesAndNodes = new Object[entriesAndNodes.length + 2];
      System.arraycopy(entriesAndNodes, 0, newEntriesAndNodes, 0, idx);
      newEntriesAndNodes[idx] = key;
      newEntriesAndNodes[idx + 1] = value;
      System.arraycopy(entriesAndNodes, idx, newEntriesAndNodes, idx + 2, entriesAndNodes.length - idx);
      return new Bitmap(hasher, seed, nodeBmp, dataBmp | pos, newEntriesAndNodes);
    }

    @Override
    Object lookup(final Object key, final long hash, final int shift) {
      final long mask = mask(hash, shift);
      final long pos = pos(mask);
      if ((dataBmp & pos) != 0) {
        final int index = index(pos, dataBmp, mask);
        return key.equals(keyAt(index)) ? valueAt(index) : null;
      } else if ((nodeBmp & pos) != 0) {
        final int index = index(pos, nodeBmp, mask);
        return nodeAt(index).lookup(key, hash, shift + BITS);
      } else return null;
    }

    static int index(final long pos, final long bitmap, final long mask) {
      return (bitmap == -1L) ? (int) mask : index(pos, bitmap);
    }

    @Override
    Dict remove(final Object key, final long hash, final int shift) {
      final long mask = mask(hash, shift);
      final long pos = pos(mask);
      if ((dataBmp & pos) != 0) {
        final int idx = index(pos, dataBmp);
        if (key.equals(keyAt(idx))) {
          if (Long.bitCount(dataBmp) == 2 && Long.bitCount(nodeBmp) == 0) {
            final long newDataBmp = (shift == 0) ? dataBmp ^ pos : pos(mask(hash, 0));
            if (idx == 0) {
              return new Bitmap(hasher, seed, 0L, newDataBmp, new Object[]{keyAt(1), valueAt(1)});
            } else {
              return new Bitmap(hasher, seed, 0L, newDataBmp, new Object[]{keyAt(0), valueAt(0)});
            }
          } else return removeKeyAndValue(pos);
        } else return this;
      } else if ((nodeBmp & pos) != 0) {
        final Dict oldSub = nodeAt(index(pos, nodeBmp));
        final Dict newSub = oldSub.remove(key, hash, shift + BITS);
        if (newSub == oldSub) {
          return this;
        }
        if (newSub instanceof Bitmap && arity(((Bitmap) newSub).dataBmp) == 1 && arity(((Bitmap) newSub).nodeBmp) == 0) {
          if (arity(dataBmp) == 0 && arity(nodeBmp) == 1) {
            return newSub;
          } else {
            return demote(pos, newSub);
          }
        } else return replaceNode(pos, newSub);
      } else return this;
    }

    Dict removeKeyAndValue(final long pos) {
      final int idx = index(pos, dataBmp) * 2;
      final Object[] newEntriesAndNodes = new Object[entriesAndNodes.length - 2];
      System.arraycopy(entriesAndNodes, 0, newEntriesAndNodes, 0, idx);
      System.arraycopy(entriesAndNodes, idx + 2, newEntriesAndNodes, idx, entriesAndNodes.length - idx - 2);
      return new Bitmap(hasher, seed, nodeBmp, dataBmp ^ pos, newEntriesAndNodes);
    }

    Dict demote(final long pos, final Dict node) {
      final int oldIdx = entriesAndNodes.length - 1 - index(pos, nodeBmp);
      final int newIdx = index(pos, dataBmp) * 2;
      final Object[] newEntriesAndNodes = new Object[entriesAndNodes.length + 1];
      System.arraycopy(entriesAndNodes, 0, newEntriesAndNodes, 0, newIdx);
      newEntriesAndNodes[newIdx] = node.keyAt(0);
      newEntriesAndNodes[newIdx + 1] = node.valueAt(0);
      System.arraycopy(entriesAndNodes, newIdx, newEntriesAndNodes, newIdx + 2, oldIdx - newIdx);
      System.arraycopy(entriesAndNodes, oldIdx + 1, newEntriesAndNodes, oldIdx + 2, entriesAndNodes.length - oldIdx - 1);
      return new Bitmap(hasher, seed, nodeBmp ^ pos, dataBmp | pos, newEntriesAndNodes);
    }

    @Override
    public Object reduce(Object[] reducer, InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException {
      final Function step = (Function) reducer[1];
      final Function complete = (Function) reducer[2];
      Object state = reducer[0];
      try {
        for (int i = 0; i < arity(dataBmp); i++) {
          state = dispatch.execute(step, state, new Tuple(keyAt(i), valueAt(i)));
        }
        for (int i = 0; i < arity(nodeBmp); i++) {
          state = nodeAt(i).fold(state, step, dispatch);
        }
      } catch (TransducerDoneException ignored) {
      }
      return dispatch.execute(complete, state);
    }

    @Override
    public Object fold(final Object initial, final Function function, final InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException {
      Object result = initial;
      for (int i = 0; i < arity(dataBmp); i++) {
        result = dispatch.execute(function, result, new Tuple(keyAt(i), valueAt(i)));
      }
      for (int i = 0; i < arity(nodeBmp); i++) {
        result = nodeAt(i).fold(result, function, dispatch);
      }
      return result;
    }

    @Override
    public <T> T fold(T initial, TriFunction<T, Object, Object, T> function) {
      T result = initial;
      for (int i = 0; i < arity(dataBmp); i++) {
        result = function.apply(result, keyAt(i), valueAt(i));
      }
      for (int i = 0; i < arity(nodeBmp); i++) {
        result = nodeAt(i).fold(result, function);
      }
      return result;
    }

    @Override
    public void forEach(final BiConsumer<? super Object, ? super Object> consumer) {
      for (int i = 0; i < arity(dataBmp); i++) {
        consumer.accept(keyAt(i), valueAt(i));
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
    public Set keys() {
      final int keys = arity(dataBmp);
      final int nodes = arity(nodeBmp);
      final Object[] data = new Object[keys + nodes];
      for (int i = 0; i < keys; i++) {
        data[i] = entriesAndNodes[i * 2];
      }
      for (int i = 0; i < nodes; i++) {
        data[keys + i] = ((Dict) entriesAndNodes[keys * 2 + i]).keys();
      }
      return new Set.Bitmap(hasher, seed, nodeBmp, dataBmp, data);
    }

    @Override
    long calculateMurmur3Hash(final long seed) {
      long hash = seed;
      for (int i = 0; i < arity(dataBmp); i++) {
        long k = Murmur3.INSTANCE.hash(seed, keyAt(i));
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
      for (int i = 0; i < arity(dataBmp); i++) {
        long k = Murmur3.INSTANCE.hash(seed, valueAt(i));
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
      if (this.entriesAndNodes.length != that.entriesAndNodes.length) {
        return false;
      }
      for (int i = 0; i < entriesAndNodes.length; i++) {
        if (!this.entriesAndNodes[i].equals(that.entriesAndNodes[i])) {
          return false;
        }
      }
      return true;
    }

    @Override
    Object keyAt(final int idx) {
      return entriesAndNodes[idx * 2];
    }

    @Override
    Object valueAt(final int idx) {
      return entriesAndNodes[idx * 2 + 1];
    }

    Dict nodeAt(final int idx) {
      return (Dict) entriesAndNodes[entriesAndNodes.length - 1 - idx];
    }

    static int index(final long pos, final long bitmap) {
      return Long.bitCount(bitmap & (pos - 1));
    }

    static int arity(final long bitmap) {
      return Long.bitCount(bitmap);
    }
  }

  static final class Collision extends Dict {
    final long commonHash;
    final Object[] entries;

    Collision(final Hasher hasher, final long seed, final long commonHash, final Object[] entries) {
      super(hasher, seed);
      this.commonHash = commonHash;
      this.entries = entries;
    }

    @Override
    Dict add(final Object key, final long hash, final Object value, final int shift) {
      for (int i = 0; i < entries.length; i += 2) {
        if (key.equals(entries[i])) {
          final Object[] newEntries = entries.clone();
          newEntries[i + 1] = value;
          return new Collision(hasher, hash, commonHash, newEntries);
        }
      }
      final Object[] newEntries = new Object[entries.length + 2];
      System.arraycopy(entries, 0, newEntries, 0, entries.length);
      newEntries[entries.length] = key;
      newEntries[entries.length + 1] = value;
      return new Collision(hasher, hash, commonHash, newEntries);
    }

    @Override
    Object lookup(final Object key, final long hash, final int shift) {
      if (hash == this.commonHash) {
        for (int i = 0; i < entries.length; i += 2) {
          if (key.equals(entries[i])) {
            return entries[i + 1];
          }
        }
      }
      return null;
    }

    @Override
    Dict remove(final Object key, final long hash, final int shift) {
      for (int i = 0; i < entries.length; i += 2) {
        if (key.equals(entries[i])) {
          if (entries.length == 2) {
            return empty(hasher, seed);
          } else {
            final Object[] newEntries = new Object[entries.length - 2];
            System.arraycopy(entries, 0, newEntries, 0, i);
            System.arraycopy(entries, i + 2, newEntries, i, entries.length - i - 2);
            return new Collision(hasher, seed, commonHash, newEntries);
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
        for (int i = 0; i < entries.length; i += 2) {
          state = dispatch.execute(step, state, new Tuple(entries[i], entries[i + 1]));
        }
      } catch (TransducerDoneException ignored) {
      }
      return dispatch.execute(complete, state);
    }

    @Override
    public Object fold(final Object initial, final Function function, final InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException {
      Object result = initial;
      for (int i = 0; i < entries.length; i += 2) {
        result = dispatch.execute(function, result, new Tuple(entries[i], entries[2]));
      }
      return result;
    }

    @Override
    public <T> T fold(T initial, TriFunction<T, Object, Object, T> function) {
      T result = initial;
      for (int i = 0; i < entries.length; i += 2) {
        result = function.apply(result, entries[i], entries[i + 1]);
      }
      return result;
    }

    @Override
    public void forEach(final BiConsumer<? super Object, ? super Object> consumer) {
      for (int i = 0; i < entries.length; i += 2) {
        consumer.accept(entries[i], entries[i + 1]);
      }
    }

    @Override
    public long size() {
      return entries.length / 2;
    }

    @Override
    public Set keys() {
      final Object[] keys = new Object[entries.length / 2];
      for (int i = 0; i < keys.length; i++) {
        keys[i] = entries[i * 2];
      }
      return new Set.Collision(hasher, seed, commonHash, keys);
    }

    @Override
    long calculateMurmur3Hash(final long seed) {
      long hash = seed;
      for (Object kv : entries) {
        hash ^= Murmur3.INSTANCE.hash(seed, kv);
      }
      return Murmur3.fMix64(hash ^ entries.length);
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
      if (this.entries.length != that.entries.length) {
        return false;
      }
      for (int i = 0; i < entries.length / 2; i += 2) {
        final Object k = entries[i];
        final Object v = entries[i + 1];
        if (!v.equals(that.lookup(k, commonHash, 0))) {
          return false;
        }
      }
      return true;
    }

    @Override
    Object keyAt(final int idx) {
      return entries[idx * 2];
    }

    @Override
    Object valueAt(final int idx) {
      return entries[idx * 2 + 1];
    }
  }
}
