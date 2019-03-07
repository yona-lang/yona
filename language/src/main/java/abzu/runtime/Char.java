package abzu.runtime;

import java.nio.charset.Charset;

public class Char {
  private static final Charset UTF_8 = Charset.forName("UTF-8");

  private final byte b0;
  private final byte b1;
  private final byte b2;
  private final byte b3;

  public Char(byte sole) {
    b0 = sole;
    b1 = 0;
    b2 = 0;
    b3 = 0;
  }

  public Char(byte fst, byte snd) {
    b0 = fst;
    b1 = snd;
    b2 = 0;
    b3 = 0;
  }

  public Char(byte fst, byte snd, byte trd) {
    b0 = fst;
    b1 = snd;
    b2 = trd;
    b3 = 0;
  }

  public Char(byte fst, byte snd, byte trd, byte fth) {
    b0 = fst;
    b1 = snd;
    b2 = trd;
    b3 = fth;
  }

  @Override
  public int hashCode() {
    return b0 << 24 | b1 << 16 | b2 << 8 | b3;
  }

  @Override
  public boolean equals(Object o) {
    if (!(o instanceof Char)) return false;
    final Char that = (Char) o;
    return this.b0 == that.b0 && this.b1 == that.b1 && this.b2 == that.b2 && this.b3 == that.b3;
  }

  @Override
  public String toString() {
    if (b3 != 0) return new String(new byte[]{ b0, b1, b2, b3 }, UTF_8);
    if (b2 != 0) return new String(new byte[]{ b0, b1, b2 }, UTF_8);
    if (b1 != 0) return new String(new byte[]{ b0, b1 }, UTF_8);
    return new String(new byte[]{ b0 }, UTF_8);
  }
}
