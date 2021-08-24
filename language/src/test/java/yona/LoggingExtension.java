package yona;

import org.junit.jupiter.api.extension.BeforeTestExecutionCallback;
import org.junit.jupiter.api.extension.ExtensionContext;

public class LoggingExtension implements BeforeTestExecutionCallback {
  @Override
  public void beforeTestExecution(ExtensionContext extensionContext) throws Exception {
    System.out.println("Running " + extensionContext.getTestClass().get() + "::" + extensionContext.getDisplayName());
  }
}
