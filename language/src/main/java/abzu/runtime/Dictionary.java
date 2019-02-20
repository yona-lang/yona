package abzu.runtime;

import com.oracle.truffle.api.interop.ForeignAccess;
import com.oracle.truffle.api.interop.MessageResolution;
import com.oracle.truffle.api.interop.Resolve;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.nodes.Node;

import java.util.Arrays;
import java.util.Objects;
import java.util.function.BiFunction;

import static java.lang.Integer.bitCount;
import static java.lang.System.arraycopy;

@MessageResolution(receiverType = Dictionary.class)
public abstract class Dictionary implements TruffleObject {

  public final Dictionary insert(Object key, Object value) {
    return insert(key, value, 0, key.hashCode());
  }

  abstract Dictionary insert(Object key, Object value, int depth, int hash);

  public final Object lookup(Object key) {
    return lookup(key, 0, key.hashCode());
  }

  abstract Object lookup(Object key, int depth, int hash);

  public final Dictionary remove(Object key) {
    final Dictionary result = remove(key, 0, key.hashCode());
    return result == null ? this : result;
  }

  abstract Dictionary remove(Object key, int depth, int hash);

  public abstract <T> T fold(BiFunction<? super T, ? super Tuple, ? extends T> function, T initial);

  public abstract int size();

  @Override
  public ForeignAccess getForeignAccess() {
    return DictionaryForeign.ACCESS;
  }

  @Resolve(message = "GET_SIZE")
  abstract static class GetSize extends Node {
    Object access(Dictionary obj) {
      return obj.size();
    }
  }

  @Resolve(message = "HAS_SIZE")
  abstract static class HasSize extends Node {
    public Object access(@SuppressWarnings("unused") Dictionary receiver) {
      return true;
    }
  }

  static boolean isInstance(TruffleObject dictionary) {
    return dictionary instanceof Dictionary;
  }

  public static Dictionary dictionary() {
    return Bitmap.EMPTY;
  }

  private static <A> A[] arraySet(A[] src, int idx, A val) {
    assert idx < src.length;
    final A[] result = src.clone();
    result[idx] = val;
    return result;
  }

  @SuppressWarnings("unchecked")
  private static <A> A[] arrayRemove(A[] src, int idx) {
    assert idx < src.length;
    A[] result = (A[]) new Object[src.length - 1];
    arraycopy(src, 0, result, 0, idx);
    arraycopy(src, idx + 1, result, idx, result.length - idx);
    return result;
  }

  private static final class Array extends Dictionary {
    final int n;
    final Dictionary[] data;
    volatile int size;

    Array(int n, Dictionary[] data) {
      this.n = n;
      this.data = data;
    }

    @Override
    Dictionary insert(Object key, Object value, int depth, int hash) {
      final int idx = (hash >>> depth) & 0x01f;
      final Dictionary dict = data[idx];
      if (dict == null) return new Array(n + 1, arraySet(data, idx, new Bitmap(key, value, depth + 5, hash)));
      final Dictionary newDict = dict.insert(key, value, depth + 5, hash);
      return dict == newDict ? this : new Array(n, arraySet(data, idx, newDict));
    }

    @Override
    Object lookup(Object key, int depth, int hash) {
      final Dictionary dict = data[(hash >>> depth) & 0x01f];
      return dict == null ? Unit.INSTANCE : dict.lookup(key, depth + 5, hash);
    }

    @Override
    Dictionary remove(Object key, int depth, int hash) {
      final int idx = (hash >>> depth) & 0x01f;
      final Dictionary dict = data[idx];
      if (dict == null) return this;
      final Dictionary newDict = dict.remove(key, depth + 5, hash);
      if (dict != newDict) {
        if (newDict != null) return new Array(n, arraySet(data, idx, newDict));
        if (n > 8) return new Array(n, arraySet(data, idx, null));
        final Object[] bmpData = new Object[n - 1];
        int bitmap = 0;
        Object cursor;
        for (int i = 0; i < data.length; i++) {
          cursor = data[i];
          if (cursor != null && i != idx) {
            bmpData[i] = cursor;
            bitmap |= 1 << i;
          }
        }
        return new Bitmap(bitmap, bmpData);
      }
      return this;
    }

    @Override
    public <T> T fold(BiFunction<? super T, ? super Tuple, ? extends T> function, T initial) {
      T result = initial;
      for (Dictionary dict : data) {
        if (dict != null) result = dict.fold(function, result);
      }
      return result;
    }

    @Override
    public int size() {
      if (size == -1) {
        int result = 0;
        for (Dictionary o : data) {
          if (o != null) result += o.size();
        }
        size = result;
      }
      return size;
    }
  }

  private static final class Bitmap extends Dictionary {
    static final Bitmap EMPTY = new Bitmap(0, new Object[]{});

    final int bitmap;
    final Object[] data;
    volatile int size = -1;

    Bitmap(int bitmap, Object[] data) {
      this.bitmap = bitmap;
      this.data = data;
    }

    Bitmap(Object key, Object value, int depth, int hash) {
      bitmap = 1 << ((hash >>> depth) & 0x01f);
      data = new Object[1];
      data[0] = new Entry(key, value);
      size = 1;
    }

    @Override
    Dictionary insert(Object key, Object value, int depth, int hash) {
      final int bit = 1 << ((hash >>> depth) & 0x01f);
      final int idx = bitCount(bitmap & (bit - 1));
      if ((bitmap & bit) != 0) {
        final Object o = data[idx];
        if (o instanceof Dictionary) {
          final Dictionary oldDict = (Dictionary) o;
          Dictionary newDict = oldDict.insert(key, value, depth + 5, hash);
          return oldDict != newDict ? new Bitmap(bitmap, arraySet(data, idx, newDict)) : this;
        } else {
          assert o instanceof Entry;
          final Entry entry = (Entry) o;
          final Object oldKey = entry.key;
          final Object oldValue = entry.value;
          if (key.equals(oldKey)) {
            return value != oldValue ? new Bitmap(bitmap, arraySet(data, idx, new Entry(key, value))) : this;
          } else {
            final int newDepth = depth + 5;
            final int oldHash = oldKey.hashCode();
            if (oldHash != hash) return new Bitmap(bitmap, arraySet(data, idx, new Bitmap(oldKey, oldValue, newDepth, oldHash).insert(key, value, newDepth, hash)));
            return new Collision(hash, 2, new Entry[]{ entry, new Entry(key, value) });
          }
        }
      } else {
        final int n = bitCount(bitmap);
        if (n < 16) {
          final Object[] newData = new Object[n + 1];
          arraycopy(data, 0, newData, 0, idx);
          arraycopy(data, idx, newData, idx + 1, n - idx);
          newData[idx] = new Entry(key, value);
          return new Bitmap(bitmap | bit, newData);
        } else {
          final Dictionary[] dicts = new Dictionary[32];
          Object o;
          int j = 0;
          for (int i = 0; i < 32; i++) {
            if (((bitmap >>> i) & 1) != 0) {
              o = data[j++];
              if (o instanceof Dictionary) {
                dicts[i] = (Dictionary) o;
              } else {
                assert o instanceof Entry;
                final Entry entry = (Entry) o;
                dicts[i] = new Bitmap(entry.key, entry.value, depth + 5, entry.key.hashCode());
              }
            }
          }
          dicts[(hash >>> depth) & 0x01f] = new Bitmap(key, value, depth + 5, hash);
          return new Array(n + 1, dicts);
        }
      }
    }

    @Override
    Object lookup(Object key, int depth, int hash) {
      final int bit = 1 << ((hash >>> depth) & 0x01f);
      if ((bitmap & bit) == 0) return null;
      int idx = bitCount(bitmap & (bit - 1));
      final Object o = data[idx];
      if(o instanceof Dictionary) {
        return ((Dictionary) o).lookup(key, depth + 5, hash);
      } else {
        assert o instanceof Entry;
        final Entry entry = (Entry) o;
        return key.equals(entry.key) ? entry.value : Unit.INSTANCE;
      }
    }

    @Override
    Dictionary remove(Object key, int depth, int hash) {
      final int bit = 1 << ((hash >>> depth) & 0x01f);
      if ((bitmap & bit) == 0) return this;
      final int idx = bitCount(bitmap & (bit - 1));
      final Object o = data[idx];
      if (o instanceof Dictionary) {
        Dictionary newDict = ((Dictionary) o).remove(key, depth + 5, hash);
        if (o == newDict) return this;
        if (newDict != null) return new Bitmap(bitmap, arraySet(data, idx, newDict));
        if (bitmap == bit) return null;
        return new Bitmap(bitmap ^ bit, arrayRemove(data, idx));
      } else {
        assert o instanceof Entry;
        return key.equals(((Entry) o).key) ? new Bitmap(bitmap ^ bit, arrayRemove(data, idx)) : this;
      }
    }

    @Override
    public <T> T fold(BiFunction<? super T, ? super Tuple, ? extends T> function, T initial) {
      T result = initial;
      for (Object o : data) {
        if (o instanceof Dictionary) {
          result = ((Dictionary) o).fold(function, result);
        } else {
          assert o instanceof Entry;
          final Entry entry = (Entry) o;
          result = function.apply(result, new Tuple(entry.key, entry.value));
        }
      }
      return result;
    }

    @Override
    public int size() {
      if (size == -1) {
        int result = 0;
        for (Object value : data) {
          result += value instanceof Dictionary ? ((Dictionary) value).size() : 1;
        }
        size = result;
      }
      return size;
    }

    @Override
    public boolean equals(Object o) {
      if (this == o) return true;
      if (o == null || getClass() != o.getClass()) return false;
      Bitmap bitmap1 = (Bitmap) o;
      return bitmap == bitmap1.bitmap &&
          size == bitmap1.size &&
          Arrays.equals(data, bitmap1.data);
    }

    @Override
    public int hashCode() {
      int result = Objects.hash(bitmap, size);
      result = 31 * result + Arrays.hashCode(data);
      return result;
    }

    @Override
    public String toString() {
      return "Bitmap{" +
          "data=" + Arrays.toString(data) +
          '}';
    }
  }

  private static final class Collision extends Dictionary {
    final int hash;
    final int n;
    final Entry[] data;

    Collision(int hash, int n, Entry[] data) {
      this.hash = hash;
      this.n = n;
      this.data = data;
    }

    private int idxOf(Object key) {
      for (int i = 0; i < n; i++) {
        if (key.equals(data[i].key)) return i;
      }
      return -1;
    }

    @Override
    Dictionary insert(Object key, Object value, int depth, int hash) {
      if(hash == this.hash) {
        final int idx = idxOf(key);
        if (idx != -1) {
          if (data[idx].value != value) {
            final Entry[] newData = data.clone();
            newData[idx] = new Entry(key, value);
            return new Collision(hash, n, newData);
          }
          return this;
        } else {
          final Entry[] newData = new Entry[n + 1];
          arraycopy(data, 0, newData, 0, n);
          newData[n] = new Entry(key, value);
          return new Collision(hash, n + 1, newData);
        }
      }
      return new Bitmap(1 << ((hash >>> depth) & 0x01f), new Object[]{ this });
    }

    @Override
    Object lookup(Object key, int depth, int hash) {
      final int idx = idxOf(key);
      if (idx == -1) {
        return null;
      } else if (key.equals(data[idx].key)) {
        return data[idx].value;
      } else return Unit.INSTANCE;
    }

    @Override
    Dictionary remove(Object key, int depth, int hash) {
      final int idx = idxOf(key);
      if (idx == -1) return this;
      if (n == 1) return null;
      return new Collision(hash, n - 1, arrayRemove(data, idx));
    }

    @Override
    public <T> T fold(BiFunction<? super T, ? super Tuple, ? extends T> function, T initial) {
      T result = initial;
      for (Object o : data) {
        if (o instanceof Dictionary) {
          result = ((Dictionary) o).fold(function, result);
        }
        if (o instanceof Entry) {
          final Entry entry = (Entry) o;
          result = function.apply(result, new Tuple(entry.key, entry.value));
        }
      }
      return result;
    }

    @Override
    public int size() {
      return n;
    }

    @Override
    public boolean equals(Object o) {
      if (this == o) return true;
      if (o == null || getClass() != o.getClass()) return false;
      Collision collision = (Collision) o;
      return hash == collision.hash &&
          n == collision.n &&
          Arrays.equals(data, collision.data);
    }

    @Override
    public int hashCode() {
      int result = Objects.hash(hash, n);
      result = 31 * result + Arrays.hashCode(data);
      return result;
    }

    @Override
    public String toString() {
      return "Collision{" +
          "data=" + Arrays.toString(data) +
          '}';
    }
  }

  private static final class Entry {
    final Object key;
    final Object value;

    Entry(Object key, Object value) {
      this.key = key;
      this.value = value;
    }

    @Override
    public boolean equals(Object o) {
      if (this == o) return true;
      if (o == null || getClass() != o.getClass()) return false;
      Entry entry = (Entry) o;
      return Objects.equals(key, entry.key) &&
          Objects.equals(value, entry.value);
    }

    @Override
    public int hashCode() {
      return Objects.hash(key, value);
    }

    @Override
    public String toString() {
      return "Entry{" +
          "key=" + key +
          ", value=" + value +
          '}';
    }
  }
}
