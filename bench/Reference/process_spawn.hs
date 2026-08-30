import System.Process (readProcess)

main :: IO ()
main = do
    output <- readProcess "seq" ["1", "10000"] ""
    print (length output)
