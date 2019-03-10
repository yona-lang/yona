package abzu.runtime;

import java.nio.charset.StandardCharsets;

public class Char {
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

  int byteLen() {
    if (b3 != 0) return 4;
    if (b2 != 0) return 3;
    if (b1 != 0) return 2;
    return 1;
  }

  void toBytes(byte[] dst, int offset) {
    if (b3 != 0) dst[offset + 3] = b3;
    if (b2 != 0) dst[offset + 2] = b2;
    if (b1 != 0) dst[offset + 1] = b1;
    dst[offset] = b0;
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
    final byte[] bytes = new byte[byteLen()];
    toBytes(bytes, 0);
    return new String(bytes, StandardCharsets.UTF_8);
  }
}
