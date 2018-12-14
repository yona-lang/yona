package abzu;

import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assume.assumeFalse;

@RunWith(AbzuTestRunner.class)
@AbzuTestSuite({"tests"})
public class AbzuSimpleTestSuite {

    public static void main(String[] args) throws Exception {
        AbzuTestRunner.runInMain(AbzuSimpleTestSuite.class, args);
    }

    @BeforeClass
    public static void before() {
        assumeFalse("Crashes on AArch64 in C2 (GR-8733)", System.getProperty("os.arch").equalsIgnoreCase("aarch64"));
    }

    /*
     * Our "mx unittest" command looks for methods that are annotated with @Test. By just defining
     * an empty method, this class gets included and the test suite is properly executed.
     */
    @Test
    public void unittest() {
    }
}
