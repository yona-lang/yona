package abzu;

import java.io.IOException;
import java.nio.file.Path;
import java.nio.file.spi.FileTypeDetector;

public final class FiletypeDetector extends FileTypeDetector {
  @Override
  public String probeContentType(Path path) throws IOException {
    if (path.getFileName().toString().endsWith(".abzu")) {
      return AbzuLanguage.MIME_TYPE;
    }
    return null;
  }
}
