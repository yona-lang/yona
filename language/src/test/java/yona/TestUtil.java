package yona;

import java.util.Collections;
import java.util.List;
import java.util.Map;

public final class TestUtil {
  public static Map<String, Long> testMap() {
    return Collections.singletonMap("one", 1L);
  }

  public static List<String> testList() {
    return Collections.singletonList("one");
  }

  public static long mapSize(Map<?, ?> map) {
    return map.size();
  }

  public static long arraySize(Object[] array) {
    return array.length;
  }
}
