package abzu.runtime;

import abzu.ast.call.DispatchNode;
import com.oracle.truffle.api.interop.ForeignAccess;
import com.oracle.truffle.api.interop.MessageResolution;
import com.oracle.truffle.api.interop.Resolve;
import com.oracle.truffle.api.interop.TruffleObject;
import com.oracle.truffle.api.nodes.Node;

import java.util.Arrays;
import java.util.Objects;

import static java.lang.Integer.bitCount;
import static java.lang.System.arraycopy;

@MessageResolution(receiverType = Dictionary.class)
public abstract class Dictionary implements TruffleObject {

  public final Dictionary insert(Object key, Object value) {
    return insert(key, value, 0, key.hashCode());
  }

  abstract Dictionary insert(Object key, Object value, int depth, int hash);

  public final Object lookup(Object key) {
    final Object result = lookup(key, 0, key.hashCode());
    return result == null ? Unit.INSTANCE : result;
  }

  abstract Object lookup(Object key, int depth, int hash);

  public final Dictionary remove(Object key) {
    final Dictionary result = remove(key, 0, key.hashCode());
    return result == null ? this : result;
  }

  abstract Dictionary remove(Object key, int depth, int hash);

  public abstract Object fold(Function fn3, Object initial, DispatchNode dispatchNode);

  public abstract Dictionary map(Function fn1, DispatchNode dispatchNode);

  public abstract int size();

  @Override
  public ForeignAccess getForeignAccess() {
    return DictionaryForeign.ACCESS;
  }

  @Resolve(message = "HAS_SIZE")
  abstract static class HasSize extends Node {
    public Object access(@SuppressWarnings("unused") Dictionary receiver) {
      return false;
    }
  }

  static boolean isInstance(TruffleObject dictionary) {
    return dictionary instanceof Dictionary;
  }

  public static Dictionary dictionary() {
    return Bitmap.EMPTY;
  }

  private static final class Array extends Dictionary {
    final int n;
    final Dictionary[] data;
    int size;

    Array(int n, Dictionary[] data) {
      this.n = n;
      this.data = data;
    }

    @Override
    Dictionary insert(Object key, Object value, int depth, int hash) {
      final int idx = (hash >>> depth) & 0x01f;
      final Dictionary dict = data[idx];
      if (dict == null) {
        final Dictionary[] newData = data.clone();
        newData[idx] = new Bitmap(key, value, depth + 5, hash);
        return new Array(n + 1, newData);
      }
      final Dictionary newDict = dict.insert(key, value, depth + 5, hash);
      if (dict != newDict) {
        final Dictionary[] newData = data.clone();
        newData[idx] = newDict;
        return new Array(n, newData);
      }
      return this;
    }

    @Override
    Object lookup(Object key, int depth, int hash) {
      final Dictionary dict = data[(hash >>> depth) & 0x01f];
      return dict == null ? null : dict.lookup(key, depth + 5, hash);
    }

    @Override
    Dictionary remove(Object key, int depth, int hash) {
      final int idx = (hash >>> depth) & 0x01f;
      final Dictionary dict = data[idx];
      if (dict == null) return this;
      final Dictionary newDict = dict.remove(key, depth + 5, hash);
      if (dict != newDict) {
        if (newDict != null) {
          final Dictionary[] newData = data.clone();
          newData[idx] = newDict;
          return new Array(n, newData);
        }
        if (n > 8) {
          final Dictionary[] newData = data.clone();
          newData[idx] = null;
          return new Array(n, newData);
        }
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
    public Object fold(Function fn3, Object initial, DispatchNode dispatchNode) {
      Object result = initial;
      for (Dictionary dict : data) {
        if (dict != null) result = dict.fold(fn3, result, dispatchNode);
      }
      return result;
    }

    @Override
    public Dictionary map(Function fn1, DispatchNode dispatchNode) {
      final int len = data.length;
      final Dictionary[] newData = new Dictionary[len];
      Dictionary cursor;
      for (int i = 0; i < len; i++) {
        cursor = data[i];
        if (cursor != null) newData[i] = cursor.map(fn1, dispatchNode);
      }
      return new Array(n, newData);
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
    int size = -1;

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
          if (oldDict != newDict) {
            final Object[] newData = data.clone();
            newData[idx] = newDict;
            return new Bitmap(bitmap, newData);
          }
          return this;
        } else {
          assert o instanceof Entry;
          final Entry entry = (Entry) o;
          final Object oldKey = entry.key;
          final Object oldValue = entry.value;
          if (key.equals(oldKey)) {
            if (value != oldValue) {
              final Object[] newData = data.clone();
              newData[idx] = new Entry(key, value);
              return new Bitmap(bitmap, newData);
            }
            return this;
          } else {
            final int newDepth = depth + 5;
            final int oldHash = oldKey.hashCode();
            if (oldHash != hash) {
              final Object[] newData = data.clone();
              newData[idx] = new Bitmap(oldKey, oldValue, newDepth, oldHash).insert(key, value, newDepth, hash);
              return new Bitmap(bitmap, newData);
            }
            return new Collision(hash, 2, new Entry[] { entry, new Entry(key, value) });
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
        return key.equals(entry.key) ? entry.value : null;
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
        if (newDict != null) {
          final Object[] newData = data.clone();
          newData[idx] = newDict;
          return new Bitmap(bitmap, newData);
        }
        if (bitmap == bit) return null;
        final Object[] newData = new Object[data.length - 1];
        arraycopy(data, 0, newData, 0, idx);
        arraycopy(data, idx + 1, newData, idx, data.length - 1 - idx);
        return new Bitmap(bitmap ^ bit, newData);
      } else {
        assert o instanceof Entry;
        if (key.equals(((Entry)o).key)) {
          final Object[] newData = new Object[data.length - 1];
          arraycopy(data, 0, newData, 0, idx);
          arraycopy(data, idx + 1, newData, idx, data.length - 1 - idx);
          return new Bitmap(bitmap ^ bit, newData);
        }
        return this;
      }
    }

    @Override
    public Object fold(Function fn3, Object initial, DispatchNode dispatchNode) {
      Object result = initial;
      for (Object o : data) {
        if (o instanceof Dictionary) {
          result = ((Dictionary) o).fold(fn3, result, dispatchNode);
        } else {
          assert o instanceof Entry;
          final Entry entry = (Entry) o;
          result = dispatchNode.executeDispatch(fn3, new Object[] { result, entry.key, entry.value });
        }
      }
      return result;
    }

    @Override
    public Dictionary map(Function fn1, DispatchNode dispatchNode) {
      final int len = data.length;
      final Object[] newData = new Object[len];
      Object cursor;
      for (int i = 0; i < len; i++) {
        cursor = data[i];
        if (cursor instanceof Dictionary) {
          newData[i] = ((Dictionary) cursor).map(fn1, dispatchNode);
        } else {
          assert cursor instanceof Entry;
          final Entry entry = (Entry) cursor;
          newData[i] = new Entry(entry.key, dispatchNode.executeDispatch(fn1, new Object[] { entry.value }));
        }
      }
      return new Bitmap(bitmap, newData);
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
          Arrays.equals(data, bitmap1.data);
    }

    @Override
    public int hashCode() {
      int result = Objects.hash(bitmap);
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

    @Override
    Dictionary insert(Object key, Object value, int depth, int hash) {
      if(hash == this.hash) {
        int idx = -1;
        for (int i = 0; i < n; i++) {
          if (key.equals(data[i].key)) {
            idx = i;
            break;
          }
        }
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
      return new Bitmap(1 << ((hash >>> depth) & 0x01f), new Object[] { this });
    }

    @Override
    Object lookup(Object key, int depth, int hash) {
      int idx = -1;
      for (int i = 0; i < n; i++) {
        if (key.equals(data[i].key)) {
          idx = i;
          break;
        }
      }
      if (idx == -1 || !key.equals(data[idx].key)) {
        return null;
      } else return data[idx].value;
    }

    @Override
    Dictionary remove(Object key, int depth, int hash) {
      int idx = -1;
      for (int i = 0; i < n; i++) {
        if (key.equals(data[i].key)) {
          idx = i;
          break;
        }
      }
      if (idx == -1) return this;
      if (n == 1) return null;
      final Entry[] newData = new Entry[data.length - 1];
      arraycopy(data, 0, newData, 0, idx);
      arraycopy(data, idx + 1, newData, idx, data.length - 1 - idx);
      return new Collision(hash, n - 1, newData);
    }

    @Override
    public Object fold(Function fn3, Object initial, DispatchNode dispatchNode) {
      Object result = initial;
      for (Object o : data) {
        if (o instanceof Dictionary) {
          result = ((Dictionary) o).fold(fn3, result, dispatchNode);
        }
        if (o instanceof Entry) {
          final Entry entry = (Entry) o;
          result = dispatchNode.executeDispatch(result, new Object[] { result, entry.key, entry.value });
        }
      }
      return result;
    }

    @Override
    public Dictionary map(Function fn1, DispatchNode dispatchNode) {
      final int len = data.length;
      final Entry[] newData = new Entry[len];
      Entry cursor;
      for (int i = 0; i < len; i++) {
        cursor = data[i];
        newData[i] = new Entry(cursor.key, dispatchNode.executeDispatch(fn1, new Object[] { cursor.value }));
      }
      return new Collision(hash, n, newData);
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
