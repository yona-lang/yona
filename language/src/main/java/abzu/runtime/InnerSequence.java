package abzu.runtime;

import static abzu.runtime.Util.*;
import static java.lang.System.arraycopy;

abstract class InnerSequence {

  abstract InnerSequence push(Object[] o, int measure);

  abstract InnerSequence inject(Object[] o, int measure);

  abstract Object[] first();

  abstract Object[] last();

  abstract InnerSequence pop();

  abstract InnerSequence eject();

  abstract int split(int idx, Split split);

  abstract int measure();

  abstract boolean empty();

  static InnerSequence catenate(InnerSequence left, InnerSequence right) {
    if (left instanceof Deep) {
      final Deep leftDeep = (Deep) left;
      if (right instanceof Deep) {
       final Deep rightDeep = (Deep) right;
       final InnerSequence leftDeeper = leftDeep.deeper();
       final InnerSequence rightDeeper = rightDeep.deeper();
       final InnerSequence newDeeper = catenate(leftDeeper.inject(leftDeep.sfx, leftDeep.sfxMeasure), rightDeeper.push(rightDeep.pfx, rightDeep.pfxMeasure));
       final int newDeeperMeasure = leftDeep.deeperMeasure + leftDeep.sfxMeasure + rightDeep.pfxMeasure + rightDeep.deeperMeasure;
       return new Deep(leftDeep.pfx, leftDeep.pfxMeasure, newDeeper, newDeeperMeasure, rightDeep.sfx, rightDeep.sfxMeasure);
      } else {
        final Shallow rightShallow = (Shallow) right;
        return rightShallow.value == null ? leftDeep : leftDeep.inject(rightShallow.value, total((byte[]) rightShallow.value[0]));
      }
    } else {
      final Shallow leftShallow = (Shallow) left;
      if (right instanceof Deep) {
        final Deep rightDeep = (Deep) right;
        return leftShallow.value == null ? rightDeep : rightDeep.push(leftShallow.value, total((byte[]) leftShallow.value[0]));
      } else {
        final Shallow rightShallow = (Shallow) right;
        if (leftShallow.value == null) return rightShallow;
        if (rightShallow.value == null) return leftShallow;
        return new Deep(leftShallow.value, total((byte[]) leftShallow.value[0]), rightShallow.value, total((byte[]) rightShallow.value[0]));
      }
    }
  }

  private static InnerSequence simplify(InnerSequence sequence) {
    if (sequence instanceof Shallow) {
      final Shallow shallow = (Shallow) sequence;
      if (shallow.value == null) return shallow;
      InnerSequence result = Shallow.EMPTY;
      final byte[] measures = (byte[]) shallow.value[0];
      int offset = 0;
      int measure;
      for (int i = 1; i < shallow.value.length; i++) {
        measure = varIntRead(measures, offset);
        result = result.inject((Object[]) shallow.value[i], measure);
        offset += varIntLen(measure);
      }
      return result;
    } else {
      final Object[] first = sequence.first();
      final int firstMeasure = total((byte[]) first[0]);
      final InnerSequence deeper = sequence.pop().eject();
      final Object[] last = sequence.last();
      final int lastMeasure = total((byte[]) last[0]);
      return new Deep(first, firstMeasure, deeper, deeper.measure(), last, lastMeasure);
    }
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
    InnerSequence pop() {
      return EMPTY;
    }

    @Override
    InnerSequence eject() {
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
    private final Object[] pfx;
    private volatile Object suspended;
    private final Object[] sfx;

    private final int pfxMeasure;
    private final int deeperMeasure;
    private final int sfxMeasure;

    InnerSequence deeper() {
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
      final byte[] pfxMeasures = new byte[varIntLen(firstMeasure)];
      varIntWrite(firstMeasure, pfxMeasures, 0);
      pfx = new Object[]{ pfxMeasures, first };
      suspended = Shallow.EMPTY;
      final byte[] sfxMeasures = new byte[varIntLen(secondMeasure)];
      varIntWrite(secondMeasure, sfxMeasures, 0);
      sfx = new Object[]{ sfxMeasures, second };
      pfxMeasure = firstMeasure;
      deeperMeasure = 0;
      sfxMeasure = secondMeasure;
    }

    Deep(Object[] pfx, int pfxMeasure, Object suspended, int deeperMeasure, Object[] sfx, int sfxMeasure) {
      this.pfx = pfx;
      this.suspended = suspended;
      this.sfx = sfx;
      this.pfxMeasure = pfxMeasure;
      this.deeperMeasure = deeperMeasure;
      this.sfxMeasure = sfxMeasure;
    }

    @Override
    InnerSequence push(Object[] o, int measure) {
      final byte[] newPfxMeasures;
      final Object[] newPfx;
      if (pfx.length == 5) {
        newPfxMeasures = new byte[varIntLen(measure)];
        varIntWrite(measure, newPfxMeasures, 0);
        newPfx = new Object[] { newPfxMeasures, o };
        final SuspendedSequence newSuspended = new SuspendedPush(deeper(), pfx, pfxMeasure, false);
        return new Deep(newPfx, measure, newSuspended, deeperMeasure + pfxMeasure, sfx, sfxMeasure);
      }
      final byte[] pfxMeasures = (byte[]) pfx[0];
      final int measureLen = varIntLen(measure);
      newPfxMeasures = new byte[pfxMeasures.length + measureLen];
      varIntWrite(measure, newPfxMeasures, 0);
      arraycopy(pfxMeasures, 0, newPfxMeasures, measureLen, pfxMeasures.length);
      newPfx = new Object[pfx.length + 1];
      newPfx[0] = newPfxMeasures;
      newPfx[1] = o;
      arraycopy(pfx, 1, newPfx, 2, pfx.length - 1);
      return new Deep(newPfx, pfxMeasure + measure, suspended, deeperMeasure, sfx, sfxMeasure);
    }

    @Override
    InnerSequence inject(Object[] o, int measure) {
      final byte[] newSfxMeasures;
      final Object[] newSfx;
      if (sfx.length == 5) {
        newSfxMeasures = new byte[varIntLen(measure)];
        varIntWrite(measure, newSfxMeasures, 0);
        newSfx = new Object[] { newSfxMeasures, o };
        final SuspendedSequence newSuspended = new SuspendedPush(deeper(), sfx, sfxMeasure, true);
        return new Deep(pfx, pfxMeasure, newSuspended, deeperMeasure + sfxMeasure, newSfx, measure);
      }
      final byte[] sfxMeasures = (byte[]) sfx[0];
      final int measureLen = varIntLen(measure);
      newSfxMeasures = new byte[sfxMeasures.length + measureLen];
      arraycopy(sfxMeasures, 0, newSfxMeasures, 0, sfxMeasures.length);
      varIntWrite(measure, newSfxMeasures, sfxMeasures.length);
      newSfx = new Object[sfx.length + 1];
      newSfx[0] = newSfxMeasures;
      arraycopy(sfx, 1, newSfx, 1, sfx.length - 1);
      newSfx[sfx.length] = o;
      return new Deep(pfx, pfxMeasure, suspended, deeperMeasure, newSfx, sfxMeasure + measure);
    }

    @Override
    Object[] first() {
      return (Object[]) pfx[1];
    }

    @Override
    Object[] last() {
      return (Object[]) sfx[sfx.length - 1];
    }

    @Override
    InnerSequence pop() {
      final byte[] newPfxMeasures;
      final Object[] newPfx;
      if (pfx.length > 2) {
        final byte[] pfxMeasures = (byte[]) pfx[0];
        final int firstMeasure = varIntRead(pfxMeasures, 0);
        final int firstMeasureLen = varIntLen(firstMeasure);
        newPfxMeasures = new byte[pfxMeasures.length - firstMeasureLen];
        arraycopy(pfxMeasures, firstMeasureLen, newPfxMeasures, 0, newPfxMeasures.length);
        newPfx = new Object[pfx.length - 1];
        newPfx[0] = newPfxMeasures;
        arraycopy(pfx, 2, newPfx, 1, newPfx.length - 1);
        return new Deep(newPfx, pfxMeasure - firstMeasure, suspended, deeperMeasure, sfx, sfxMeasure);
      }
      if (deeperMeasure != 0) {
        final InnerSequence deeper = deeper();
        newPfx = deeper.first();
        final int newPfxMeasure = total((byte[]) newPfx[0]);
        final SuspendedSequence newSuspended = new SuspendedPop(deeper, false);
        return new Deep(newPfx, newPfxMeasure, newSuspended, deeperMeasure - newPfxMeasure, sfx, sfxMeasure);
      }
      if (sfx.length > 2) {
        final byte[] sfxMeasures = (byte[]) sfx[0];
        final int newPfxMeasure = varIntRead(sfxMeasures, 0);
        final int newPfxMeasureLen = varIntLen(newPfxMeasure);
        newPfxMeasures = new byte[newPfxMeasureLen];
        varIntWrite(newPfxMeasure, newPfxMeasures, 0);
        newPfx = new Object[] { newPfxMeasures, sfx[1] };
        final byte[] newSfxMeasures = new byte[sfxMeasures.length - newPfxMeasureLen];
        arraycopy(sfxMeasures, newPfxMeasureLen, newSfxMeasures, 0, newSfxMeasures.length);
        final Object[] newSfx = new Object[sfx.length - 1];
        newSfx[0] = newSfxMeasures;
        arraycopy(sfx, 2, newSfx, 1, newSfx.length - 1);
        return new Deep(newPfx, newPfxMeasure, Shallow.EMPTY, 0, newSfx, sfxMeasure - newPfxMeasure);
      }
      return new Shallow((Object[]) sfx[1]);
    }

    @Override
    InnerSequence eject() {
      final byte[] newSfxMeasures;
      final Object[] newSfx;
      if (sfx.length > 2) {
        final byte[] sfxMeasures = (byte[]) sfx[0];
        int lastMeasure = 0;
        int lastMeasureLen = 0;
        int offset = 0;
        while (offset < sfxMeasures.length) {
          lastMeasure = varIntRead(sfxMeasures, offset);
          lastMeasureLen = varIntLen(lastMeasure);
          offset += lastMeasureLen;
        }
        newSfxMeasures = new byte[sfxMeasures.length - lastMeasureLen];
        arraycopy(sfxMeasures, 0, newSfxMeasures, 0, newSfxMeasures.length);
        newSfx = new Object[sfx.length - 1];
        newSfx[0] = newSfxMeasures;
        arraycopy(sfx, 1, newSfx, 1, newSfx.length - 1);
        return new Deep(pfx, pfxMeasure, suspended, deeperMeasure, newSfx, sfxMeasure - lastMeasure);
      }
      if (deeperMeasure != 0) {
        final InnerSequence deeper = deeper();
        newSfx = deeper.last();
        final int newSfxMeasure = total((byte[]) newSfx[0]);
        final SuspendedSequence newSuspended = new SuspendedPop(deeper, true);
        return new Deep(pfx, pfxMeasure, newSuspended, deeperMeasure - newSfxMeasure, newSfx, newSfxMeasure);
      }
      if (pfx.length > 2) {
        final byte[] pfxMeasures = (byte[]) pfx[0];
        int newSuffixMeasure = 0;
        int newSuffixMasureLen = 0;
        int offset = 0;
        while (offset < pfxMeasures.length) {
          newSuffixMeasure = varIntRead(pfxMeasures, offset);
          newSuffixMasureLen = varIntLen(newSuffixMeasure);
          offset += newSuffixMasureLen;
        }
        newSfxMeasures = new byte[newSuffixMasureLen];
        varIntWrite(newSuffixMeasure, newSfxMeasures, 0);
        newSfx = new Object[] { newSfxMeasures, pfx[pfx.length - 1] };
        final byte[] newPfxMeasures = new byte[pfxMeasures.length - newSuffixMasureLen];
        arraycopy(pfxMeasures, 0, newPfxMeasures, 0, newPfxMeasures.length);
        final Object[] newPrefix = new Object[pfx.length - 1];
        newPrefix[0] = newPfxMeasures;
        arraycopy(pfx, 1, newPrefix, 1, newPrefix.length - 1);
        return new Deep(newPrefix, pfxMeasure - newSuffixMeasure, Shallow.EMPTY, 0, newSfx, newSuffixMeasure);
      }
      return new Shallow((Object[]) pfx[1]);
    }

    @Override
    int split(int idx, Split split) {
      final boolean trackLeft = split.left != null;
      final boolean trackRight = split.right != null;
      if (idx < pfxMeasure) {
        if (trackRight) {
          split.right = catenate(sfx, split.right);
          split.right = catenate(simplify(deeper()), split.right);
        }
        return split(pfx, idx, split);
      }
      idx -= pfxMeasure;
      if (idx < deeperMeasure) {
        if (trackLeft) split.left = catenate(split.left, pfx);
        if (trackRight) split.right = catenate(sfx, split.right);
        final Split deeperSplit = new Split(trackLeft, trackRight);
        idx = deeper().split(idx, deeperSplit);
        if (trackLeft) split.left = catenate(split.left, simplify(deeperSplit.left));
        if (trackRight) split.right = catenate(simplify(deeperSplit.right), split.right);
        return split(deeperSplit.point, idx, split);
      }
      idx -= deeperMeasure;
      if (idx < sfxMeasure) {
        if (trackLeft) {
          split.left = catenate(split.left, pfx);
          split.left = catenate(split.left, simplify(deeper()));
        }
        return split(sfx, idx, split);
      }
      idx -= sfxMeasure;
      return idx;
    }

    private static InnerSequence catenate(Object[] sfx, InnerSequence seq) {
      final byte[] measures = (byte[]) sfx[0];
      int offset = measures.length - 1;
      for (int i = sfx.length - 1; i > 0; i--) {
        while (offset != 0) {
          if ((0x80 & measures[offset - 1]) == 0) break;
          offset--;
        }
        seq = seq.push((Object[]) sfx[i], varIntRead(measures, offset));
        offset--;
      }
      return seq;
    }

    private static InnerSequence catenate(InnerSequence seq, Object[] pfx) {
      final byte[] measures = (byte[]) pfx[0];
      int offset = 0;
      int measure;
      for (int i = 1; i < pfx.length; i++) {
        measure = varIntRead(measures, offset);
        seq = seq.inject((Object[]) pfx[i], measure);
        offset += varIntLen(measure);
      }
      return seq;
    }

    private static int split(Object[] afx, int idx, Split split) {
      final boolean trackLeft = split.left != null;
      final boolean trackRight = split.right != null;
      final byte[] measures = (byte[]) afx[0];
      int offset = 0;
      int afxIdx = 1;
      int measure;
      while (afxIdx < afx.length) {
        measure = varIntRead(measures, offset);
        if (idx < measure) break;
        if (trackLeft) split.left = split.left.inject((Object[]) afx[afxIdx], measure);
        idx -= measure;
        offset += varIntLen(measure);
        afxIdx++;
      }
      split.point = (Object[]) afx[afxIdx];
      if (trackRight) {
        offset = measures.length - 1;
        for (int i = afx.length - 1; i > afxIdx; i--) {
          while (offset != 0) {
            if ((0x80 & measures[offset - 1]) == 0) break;
            offset--;
          }
          measure = varIntRead(measures, offset);
          split.right = split.right.push((Object[]) afx[i], measure);
          offset--;
        }
      }
      return idx;
    }

    @Override
    int measure() {
      return pfxMeasure + deeperMeasure + sfxMeasure;
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
      return inverted ? sequence.pop() : sequence.eject();
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
