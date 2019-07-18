package yatta.runtime.strings;

public final class StringUtil {
  public static String yattaValueAsYattaString(boolean val) {
    return String.valueOf(val);
  }

  public static String yattaValueAsYattaString(byte val) {
    return String.valueOf(val);
  }

  public static String yattaValueAsYattaString(long val) {
    return String.valueOf(val);
  }

  public static String yattaValueAsYattaString(double val) {
    return String.valueOf(val);
  }

  public static String yattaValueAsYattaString(String val) {
    return val;
  }

  public static String yattaValueAsYattaString(Object val) {
    return val.toString();
  }
}
