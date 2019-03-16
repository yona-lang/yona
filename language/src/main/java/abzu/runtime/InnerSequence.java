package abzu.runtime;

import static abzu.runtime.Util.*;
import static java.lang.System.arraycopy;

abstract class InnerSequence {

  abstract InnerSequence push(Object[] o, int measure);

  abstract InnerSequence inject(Object[] o, int measure);

  abstract Object[] first();

  abstract Object[] last();

  abstract InnerSequence removeFirst();

  abstract InnerSequence removeLast();

  abstract int split(int idx, Split split);

  abstract int measure();

  abstract boolean empty();

  static InnerSequence catenate(InnerSequence left, InnerSequence right) {
    return null; // TODO
  }

  private static int total(byte[] measures) {
    int result = 0;
    int offset = 0;
    int increment;
    while (offset < measures.length) {
      increment = varIntRead(measures, offset);
      offset += varIntLen(increment);
      result += increment;
    }
    return result;
  }

  static final class Shallow extends InnerSequence {
    static final Shallow EMPTY = new Shallow(null);

    private final Object[] value;

    Shallow(Object[] value) {
      this.value = value;
    }

    @Override
    InnerSequence push(Object[] o, int measure) {
      return value == null ? new Shallow(o) : new Deep(o, measure, value, total((byte[]) value[0]));
    }

    @Override
    InnerSequence inject(Object[] o, int measure) {
      return value == null ? new Shallow(o) : new Deep(value, total((byte[]) value[0]), o, measure);
    }

    @Override
    Object[] first() {
      return value;
    }

    @Override
    Object[] last() {
      return value;
    }

    @Override
    InnerSequence removeFirst() {
      return EMPTY;
    }

    @Override
    InnerSequence removeLast() {
      return EMPTY;
    }

    @Override
    int split(int idx, Split split) {
      split.point = value;
      return idx;
    }

    @Override
    int measure() {
      return value == null ? 0 : total((byte[]) value[0]);
    }

    @Override
    boolean empty() {
      return value == null;
    }
  }

  static final class Deep extends InnerSequence {
    private final Object[] prefix;
    private volatile Object suspended;
    private final Object[] suffix;

    private final int prefixMeasure;
    private final int innerMeasure;
    private final int suffixMeasure;

    InnerSequence innerSequence() {
      boolean done = (suspended instanceof InnerSequence);
      if (!done) {
        synchronized (this) {
          done = (suspended instanceof InnerSequence);
          if (!done) {
            suspended = ((SuspendedSequence) suspended).get();
          }
        }
      }
      return (InnerSequence) suspended;
    }

    Deep(Object[] first, int firstMeasure, Object[] second, int secondMeasure) {
      prefixMeasure = firstMeasure;
      final byte[] prefixMeta = new byte[varIntLen(firstMeasure)];
      varIntWrite(firstMeasure, prefixMeta, 0);
      prefix = new Object[]{ prefixMeta, first };
      suffixMeasure = secondMeasure;
      final byte[] suffixMeta = new byte[varIntLen(secondMeasure)];
      varIntWrite(secondMeasure, suffixMeta, 0);
      suffix = new Object[]{ suffixMeta, second };
      suspended = Shallow.EMPTY;
      innerMeasure = 0;
    }

    Deep(Object[] prefix, int prefixMeasure, Object suspended, int innerMeasure, Object[] suffix, int suffixMeasure) {
      this.prefix = prefix;
      this.suspended = suspended;
      this.suffix = suffix;
      this.prefixMeasure = prefixMeasure;
      this.innerMeasure = innerMeasure;
      this.suffixMeasure = suffixMeasure;
    }

    @Override
    InnerSequence push(Object[] o, int measure) {
      final byte[] newPrefixMeta;
      final Object[] newPrefix;
      if (prefix.length == 5) {
        newPrefixMeta = new byte[varIntLen(measure)];
        varIntWrite(measure, newPrefixMeta, 0);
        newPrefix = new Object[] { newPrefixMeta, o };
        final SuspendedSequence newSuspended = new SuspendedPush(innerSequence(), prefix, prefixMeasure, false);
        return new Deep(newPrefix, measure, newSuspended, innerMeasure + prefixMeasure, suffix, suffixMeasure);
      }
      final byte[] prefixMeta = (byte[]) prefix[0];
      final int measureLen = varIntLen(measure);
      newPrefixMeta = new byte[prefixMeta.length + measureLen];
      varIntWrite(measure, newPrefixMeta, 0);
      arraycopy(prefixMeta, 0, newPrefixMeta, measureLen, prefixMeta.length);
      newPrefix = new Object[prefix.length + 1];
      newPrefix[0] = newPrefixMeta;
      newPrefix[1] = o;
      arraycopy(prefix, 1, newPrefix, 2, prefix.length - 1);
      return new Deep(newPrefix, prefixMeasure + measure, suspended, innerMeasure, suffix, suffixMeasure);
    }

    @Override
    InnerSequence inject(Object[] o, int measure) {
      final byte[] newSuffixMeta;
      final Object[] newSuffix;
      if (suffix.length == 5) {
        newSuffixMeta = new byte[varIntLen(measure)];
        varIntWrite(measure, newSuffixMeta, 0);
        newSuffix = new Object[] { newSuffixMeta, o };
        final SuspendedSequence newSuspended = new SuspendedPush(innerSequence(), suffix, suffixMeasure, true);
        return new Deep(prefix, prefixMeasure, newSuspended, innerMeasure + suffixMeasure, newSuffix, measure);
      }
      final byte[] suffixMeta = (byte[]) suffix[0];
      final int measureLen = varIntLen(measure);
      newSuffixMeta = new byte[suffixMeta.length + measureLen];
      arraycopy(suffixMeta, 0, newSuffixMeta, 0, suffixMeta.length);
      varIntWrite(measure, newSuffixMeta, suffixMeta.length);
      newSuffix = new Object[suffix.length + 1];
      newSuffix[0] = newSuffixMeta;
      arraycopy(suffix, 1, newSuffix, 1, suffix.length - 1);
      newSuffix[suffix.length] = o;
      return new Deep(prefix, prefixMeasure, suspended, innerMeasure, newSuffix, suffixMeasure + measure);
    }

    @Override
    Object[] first() {
      return (Object[]) prefix[1];
    }

    @Override
    Object[] last() {
      return (Object[]) suffix[suffix.length - 1];
    }

    @Override
    InnerSequence removeFirst() {
      final byte[] newPrefixMeta;
      final Object[] newPrefix;
      if (prefix.length > 2) {
        final byte[] prefixMeta = (byte[]) prefix[0];
        final int firstMeasure = varIntRead(prefixMeta, 0);
        final int firstMeasureLen = varIntLen(firstMeasure);
        newPrefixMeta = new byte[prefixMeta.length - firstMeasureLen];
        arraycopy(prefixMeta, firstMeasureLen, newPrefixMeta, 0, newPrefixMeta.length);
        newPrefix = new Object[prefix.length - 1];
        newPrefix[0] = newPrefixMeta;
        arraycopy(prefix, 2, newPrefix, 1, newPrefix.length - 1);
        return new Deep(newPrefix, prefixMeasure - firstMeasure, suspended, innerMeasure, suffix, suffixMeasure);
      }
      if (innerMeasure != 0) {
        final InnerSequence innerSequence = innerSequence();
        newPrefix = innerSequence.first();
        final int newPrefixMeasure = total((byte[]) newPrefix[0]);
        final SuspendedSequence newSuspended = new SuspendedPop(innerSequence, false);
        return new Deep(newPrefix, newPrefixMeasure, newSuspended, innerMeasure - newPrefixMeasure, suffix, suffixMeasure);
      }
      if (suffix.length > 2) {
        final byte[] suffixMeta = (byte[]) suffix[0];
        final int newPrefixMeasure = varIntRead(suffixMeta, 0);
        final int newPrefixMeasureLen = varIntLen(newPrefixMeasure);
        newPrefixMeta = new byte[newPrefixMeasureLen];
        varIntWrite(newPrefixMeasure, newPrefixMeta, 0);
        newPrefix = new Object[] { newPrefixMeta, suffix[1] };
        final byte[] newSuffixMeta = new byte[suffixMeta.length - newPrefixMeasureLen];
        arraycopy(suffixMeta, newPrefixMeasureLen, newSuffixMeta, 0, newSuffixMeta.length);
        final Object[] newSuffix = new Object[suffix.length - 1];
        newSuffix[0] = newSuffixMeta;
        arraycopy(suffix, 2, newSuffix, 1, newSuffix.length - 1);
        return new Deep(newPrefix, newPrefixMeasure, Shallow.EMPTY, 0, newSuffix, suffixMeasure - newPrefixMeasure);
      }
      return new Shallow((Object[]) suffix[1]);
    }


    @Override
    InnerSequence removeLast() {
      final byte[] newSuffixMeta;
      final Object[] newSuffix;
      if (suffix.length > 2) {
        final byte[] suffixMeta = (byte[]) suffix[0];
        int lastMeasure = 0;
        int lastMeasureLen = 0;
        int offset = 0;
        while (offset < suffixMeta.length) {
          lastMeasure = varIntRead(suffixMeta, offset);
          lastMeasureLen = varIntLen(lastMeasure);
          offset += lastMeasureLen;
        }
        newSuffixMeta = new byte[suffixMeta.length - lastMeasureLen];
        arraycopy(suffixMeta, 0, newSuffixMeta, 0, newSuffixMeta.length);
        newSuffix = new Object[suffix.length - 1];
        newSuffix[0] = newSuffixMeta;
        arraycopy(suffix, 1, newSuffix, 1, newSuffix.length - 1);
        return new Deep(prefix, prefixMeasure, suspended, innerMeasure, newSuffix, suffixMeasure - lastMeasure);
      }
      if (innerMeasure != 0) {
        final InnerSequence innerSequence = innerSequence();
        newSuffix = innerSequence.last();
        final int newSuffixMeasure = total((byte[]) newSuffix[0]);
        final SuspendedSequence newSuspended = new SuspendedPop(innerSequence, true);
        return new Deep(prefix, prefixMeasure, newSuspended, innerMeasure - newSuffixMeasure, newSuffix, newSuffixMeasure);
      }
      if (prefix.length > 2) {
        final byte[] prefixMeta = (byte[]) prefix[0];
        int newSuffixMeasure = 0;
        int newSuffixMasureLen = 0;
        int offset = 0;
        while (offset < prefixMeta.length) {
          newSuffixMeasure = varIntRead(prefixMeta, offset);
          newSuffixMasureLen = varIntLen(newSuffixMeasure);
          offset += newSuffixMasureLen;
        }
        newSuffixMeta = new byte[newSuffixMasureLen];
        varIntWrite(newSuffixMeasure, newSuffixMeta, 0);
        newSuffix = new Object[] { newSuffixMeta, prefix[prefix.length - 1] };
        final byte[] newPrefixMeta = new byte[prefixMeta.length - newSuffixMasureLen];
        arraycopy(prefixMeta, 0, newPrefixMeta, 0, newPrefixMeta.length);
        final Object[] newPrefix = new Object[prefix.length - 1];
        newPrefix[0] = newPrefixMeta;
        arraycopy(prefix, 1, newPrefix, 1, newPrefix.length - 1);
        return new Deep(newPrefix, prefixMeasure - newSuffixMeasure, Shallow.EMPTY, 0, newSuffix, newSuffixMeasure);
      }
      return new Shallow((Object[]) prefix[1]);
    }

    @Override
    int split(int idx, Split split) {
      return 0; // TODO
    }

    @Override
    int measure() {
      return prefixMeasure + innerMeasure + suffixMeasure;
    }

    @Override
    boolean empty() {
      return false;
    }
  }

  static abstract class SuspendedSequence {
    abstract InnerSequence get();
  }

  static final class SuspendedPush extends SuspendedSequence {
    final InnerSequence sequence;
    final Object[] o;
    final int measure;
    final boolean inverted;

    SuspendedPush(InnerSequence sequence, Object[] o, int measure, boolean inverted) {
      this.sequence = sequence;
      this.o = o;
      this.measure = measure;
      this.inverted = inverted;
    }

    @Override
    InnerSequence get() {
      return inverted ? sequence.inject(o, measure) : sequence.push(o, measure);
    }
  }

  static final class SuspendedPop extends SuspendedSequence {
    final InnerSequence sequence;
    final boolean inverted;

    SuspendedPop(InnerSequence sequence, boolean inverted) {
      this.sequence = sequence;
      this.inverted = inverted;
    }

    @Override
    InnerSequence get() {
      return inverted ? sequence.removeLast() : sequence.removeFirst();
    }
  }

  static final class Split {
    InnerSequence left;
    Object[] point;
    InnerSequence right;

    Split(boolean trackLeft, boolean trackRight) {
      if (trackLeft) left = Shallow.EMPTY;
      if (trackRight) left = Shallow.EMPTY;
    }
  }
}
