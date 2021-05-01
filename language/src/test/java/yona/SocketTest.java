package yona;

import org.junit.jupiter.api.Tag;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;

import java.io.File;
import java.io.IOException;

@ExtendWith(LoggingExtension.class)
public class SocketTest {
  @Test
  @Tag("slow")
  public void echoServerTest() throws IOException, InterruptedException {
    int result = new ProcessBuilder("bash", "-l", "run.sh").directory(new File("../tests/echo-server")).start().waitFor();
    assert 0 == result;
  }
}
