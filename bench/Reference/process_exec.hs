import System.Process (readProcess)

main :: IO ()
main = do
    a <- readProcess "echo" ["hello"] ""
    b <- readProcess "echo" ["world"] ""
    c <- readProcess "echo" ["yona"] ""
    print (length (init a) + length (init b) + length (init c))
