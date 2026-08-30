import Control.Concurrent.Async (mapConcurrently)

main :: IO ()
main = do
    let path = "bench/data/bench_text.txt"
    results <- mapConcurrently readFile [path, path, path]
    print (sum (map length results))
