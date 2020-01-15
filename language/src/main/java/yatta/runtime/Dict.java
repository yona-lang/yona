package yatta.runtime;

import com.oracle.truffle.api.CompilerDirectives;
import com.oracle.truffle.api.interop.*;
import com.oracle.truffle.api.library.ExportLibrary;
import com.oracle.truffle.api.library.ExportMessage;
import yatta.common.TriFunction;

@ExportLibrary(InteropLibrary.class)
public abstract class Dict implements TruffleObject {
  static final int BITS = 6;
  static final int MASK = 0x3f;

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
    return lookup(key, hasher.hash(seed, key), 0);
  }

  abstract Object lookup(final Object key, final long hash, final int shift);

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public final Dict remove(final Object key) {
    return remove(key, hasher.hash(seed, key), 0);
  }

  abstract Dict remove(Object key, long hash, int shift);

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public abstract Object fold(Object initial, Function function, InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException;

  @CompilerDirectives.TruffleBoundary(allowInlining = true)
  public abstract <T> T fold(final T initial, final TriFunction<T, Object, Object, T> function);

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
      return new Collision(hasher, seed, fstHash, new Object[]{ fstKey, sndKey }, new Object[]{ fstValue, sndValue });
    }
    final long fstMask = mask(fstHash, shift);
    final long sndMask = mask(sndHash, shift);
    if (fstMask < sndMask) {
      return new Bitmap(hasher, seed, 0, pos(fstMask) | pos(sndMask), new Object[]{ fstKey, sndKey }, new Object[]{ fstValue, sndValue });
    } else if (fstMask > sndMask) {
      return new Bitmap(hasher, seed, 0, pos(fstMask) | pos(sndMask), new Object[]{ sndKey, fstKey }, new Object[]{ sndValue, fstValue });
    } else {
      return new Bitmap(hasher, seed, pos(fstMask), 0, new Object[]{ merge(fstKey, fstHash, fstValue, sndKey, sndHash, sndValue, shift + BITS) }, EMPTY_ARRAY);
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
    return new Bitmap(hasher, seed, 0L, 0L, EMPTY_ARRAY, EMPTY_ARRAY);
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
    final Object[] keysAndNodes;
    final Object[] values;

    Bitmap(final Hasher hasher, final long seed, final long nodeBmp, final long dataBmp, final Object[] keysAndNodes, final Object[] values) {
      super(hasher, seed);
      this.nodeBmp = nodeBmp;
      this.dataBmp = dataBmp;
      this.keysAndNodes = keysAndNodes;
      this.values = values;
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
      final int oldIdx = index(pos, dataBmp);
      final int newIdx = keysAndNodes.length - 1 - index(pos, nodeBmp);
      final Object[] newKeysAndNodes = new Object[keysAndNodes.length - 1 + 1];
      System.arraycopy(keysAndNodes, 0, newKeysAndNodes, 0, oldIdx);
      System.arraycopy(keysAndNodes, oldIdx + 1, newKeysAndNodes, oldIdx, newIdx - oldIdx);
      newKeysAndNodes[newIdx] = node;
      System.arraycopy(keysAndNodes, newIdx + 1, newKeysAndNodes, newIdx + 1, keysAndNodes.length - newIdx - 1);
      final Object[] newValues = new Object[values.length - 1];
      System.arraycopy(values, 0, newValues, 0, oldIdx);
      System.arraycopy(values, oldIdx + 1, newValues, oldIdx, values.length - oldIdx - 1);
      return new Bitmap(hasher, seed, nodeBmp | pos, dataBmp ^ pos, newKeysAndNodes, newValues);
    }

    Dict replaceValue(final int idx, final Object value) {
      final Object[] newValues = values.clone();
      newValues[idx] = value;
      return new Bitmap(hasher, seed, nodeBmp, dataBmp, keysAndNodes, newValues);
    }

    Dict replaceNode(final long pos, final Dict node) {
      final Object[] newKeysAndNodes = keysAndNodes.clone();
      newKeysAndNodes[keysAndNodes.length - 1 - index(pos, nodeBmp)] = node;
      return new Bitmap(hasher, seed, nodeBmp, dataBmp, newKeysAndNodes, values);
    }

    Dict insertKeyAndValue(final long pos, final Object key, final Object value) {
      final int idx = index(pos, dataBmp);
      final Object[] newKeysAndNodes = new Object[keysAndNodes.length + 1];
      System.arraycopy(keysAndNodes, 0, newKeysAndNodes, 0, idx);
      newKeysAndNodes[idx] = key;
      System.arraycopy(keysAndNodes, idx, newKeysAndNodes, idx + 1, keysAndNodes.length - idx);
      final Object[] newValues = new Object[values.length + 1];
      System.arraycopy(values, 0, newValues, 0, idx);
      newValues[idx] = value;
      System.arraycopy(values, idx, newValues, idx + 1, values.length - idx);
      return new Bitmap(hasher, seed, nodeBmp, dataBmp | pos, newKeysAndNodes, newValues);
    }

    @Override
    Object lookup(final Object key, final long hash, final int shift) {
      final long mask = mask(hash, shift);
      final long pos = pos(mask);
      if ((dataBmp & pos) != 0) {
        final int index = index(pos, dataBmp, mask);
        return key.equals(keyAt(index)) ? valueAt(index) : Unit.INSTANCE;
      } else if ((nodeBmp & pos) != 0) {
        final int index = index(pos, nodeBmp, mask);
        return nodeAt(index).lookup(key, hash, shift + BITS);
      } else return Unit.INSTANCE;
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
              return new Bitmap(hasher, seed, 0L, newDataBmp, new Object[]{ keyAt(1) }, new Object[]{ valueAt(1) });
            } else {
              return new Bitmap(hasher, seed, 0L, newDataBmp, new Object[]{ keyAt(0) }, new Object[]{ valueAt(0) });
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
      final int idx = index(pos, dataBmp);
      final Object[] newKeysAndNodes = new Object[keysAndNodes.length - 1];
      System.arraycopy(keysAndNodes, 0, newKeysAndNodes, 0, idx);
      System.arraycopy(keysAndNodes, idx + 1, newKeysAndNodes, idx, keysAndNodes.length - idx - 1);
      final Object[] newValues = new Object[values.length - 1];
      System.arraycopy(values, 0, newValues, 0, idx);
      System.arraycopy(values, idx + 1, newValues, idx, values.length - idx - 1);
      return new Bitmap(hasher, seed, nodeBmp, dataBmp ^ pos, newKeysAndNodes, newValues);
    }

    Dict demote(final long pos, final Dict node) {
      final int oldIdx = keysAndNodes.length - 1 - index(pos, nodeBmp);
      final int newIdx = index(pos, dataBmp);
      final Object[] newKeysAndNodes = new Object[keysAndNodes.length];
      System.arraycopy(keysAndNodes, 0, newKeysAndNodes, 0, newIdx);
      newKeysAndNodes[newIdx] = node.keyAt(0);
      System.arraycopy(keysAndNodes, newIdx, newKeysAndNodes, newIdx + 1, oldIdx - newIdx);
      System.arraycopy(keysAndNodes, oldIdx + 1, newKeysAndNodes, oldIdx + 1, keysAndNodes.length - oldIdx - 1);
      final Object[] newValues = new Object[values.length - 1];
      System.arraycopy(values, 0, newValues, 0, newIdx);
      newValues[newIdx] = node.valueAt(0);
      System.arraycopy(values, newIdx, newValues, newIdx + 1, values.length - newIdx - 1);
      return new Bitmap(hasher, seed, nodeBmp ^ pos, dataBmp | pos, newKeysAndNodes, newValues);
    }

    @Override
    public Object fold(final Object initial, final Function function, final InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException {
      Object result = initial;
      for (int i = 0; i < arity(dataBmp); i++) {
        result = dispatch.execute(function, result, keyAt(i), valueAt(i));
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
    public long size() {
      long result = arity(dataBmp);
      for (int i = 0; i < arity(nodeBmp); i++) {
        result += nodeAt(i).size();
      }
      return result;
    }

    @Override
    public Set keys() {
      final Object[] data = keysAndNodes.clone();
      for (int i = 0; i < arity(nodeBmp); i++) {
        data[data.length - 1 - i] = nodeAt(i).keys();
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
      if (this.keysAndNodes.length != that.keysAndNodes.length) {
        return false;
      }
      for (int i = 0; i < keysAndNodes.length; i++) {
        if (!this.keysAndNodes[i].equals(that.keysAndNodes[i])) {
          return false;
        }
      }
      for (int i = 0; i < values.length; i++) {
        if (!this.values[i].equals(that.values[i])) {
          return false;
        }
      }
      return true;
    }

    @Override
    Object keyAt(final int idx) {
      return keysAndNodes[idx];
    }

    @Override
    Object valueAt(final int idx) {
      return values[idx];
    }

    Dict nodeAt(final int idx) {
      return (Dict) keysAndNodes[keysAndNodes.length - 1 - idx];
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
    final Object[] keys;
    final Object[] values;

    Collision(final Hasher hasher, final long seed, final long commonHash, final Object[] keys, final Object[] values) {
      super(hasher, seed);
      this.commonHash = commonHash;
      this.keys = keys;
      this.values = values;
    }

    @Override
    Dict add(final Object key, final long hash, final Object value, final int shift) {
      for (int i = 0; i < keys.length; i++) {
        if (key.equals(keys[i])) {
          final Object[] newValues = values.clone();
          newValues[i] = value;
          return new Collision(hasher, hash, commonHash, keys, newValues);
        }
      }
      final Object[] newKeys = new Object[keys.length + 1];
      System.arraycopy(keys, 0, newKeys, 0, keys.length);
      newKeys[keys.length] = key;
      final Object[] newValues = new Object[values.length + 1];
      System.arraycopy(values, 0, newValues, 0, values.length);
      newValues[values.length] = value;
      return new Collision(hasher, hash, commonHash, newKeys, newValues);
    }

    @Override
    Object lookup(final Object key, final long hash, final int shift) {
      if (hash == this.commonHash) {
        for (int i = 0; i < keys.length; i++) {
          if (key.equals(keys[i])) {
            return values[i];
          }
        }
      }
      return Unit.INSTANCE;
    }

    @Override
    Dict remove(final Object key, final long hash, final int shift) {
      for (int i = 0; i < keys.length; i++) {
        if (key.equals(keys[i])) {
          if (keys.length == 1) {
            return empty(hasher, seed);
          } else {
            final Object[] newKeys = new Object[keys.length - 1];
            System.arraycopy(keys, 0, newKeys, 0, i);
            System.arraycopy(keys, i + 1, newKeys, i, keys.length - i - 1);
            final Object[] newValues = new Object[values.length - 1];
            System.arraycopy(values, 0, newValues, 0, i);
            System.arraycopy(values, i + 1, newValues, i, values.length - i - 1);
            return new Collision(hasher, seed, commonHash, newKeys, newValues);
          }
        }
      }
      return this;
    }

    @Override
    public Object fold(final Object initial, final Function function, final InteropLibrary dispatch) throws UnsupportedMessageException, ArityException, UnsupportedTypeException {
      Object result = initial;
      for (int i = 0; i < keys.length; i++) {
        result = dispatch.execute(function, result, keyAt(i), valueAt(i));
      }
      return result;
    }

    @Override
    public <T> T fold(T initial, TriFunction<T, Object, Object, T> function) {
      T result = initial;
      for (int i = 0; i < keys.length; i++) {
        result = function.apply(result, keyAt(i), valueAt(i));
      }
      return result;
    }

    @Override
    public long size() {
      return keys.length;
    }

    @Override
    public Set keys() {
      return new Set.Collision(hasher, seed, commonHash, keys);
    }

    @Override
    long calculateMurmur3Hash(final long seed) {
      long hash = seed;
      for (Object key : keys) {
        hash ^= Murmur3.INSTANCE.hash(seed, key);
      }
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
      if (this.keys.length != that.keys.length) {
        return false;
      }
      for (int i = 0; i < keys.length; i++) {
        if (!this.values[i].equals(that.lookup(keys[i], commonHash, 0))) {
          return false;
        }
      }
      return true;
    }

    @Override
    Object keyAt(final int idx) {
      return keys[idx];
    }

    @Override
    Object valueAt(final int idx) {
      return values[idx];
    }
  }
}
