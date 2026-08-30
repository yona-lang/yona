main :: IO ()
main = do
    data' <- readFile "bench/data/bench_text.txt"
    writeFile "/tmp/yona_bench_write.txt" data'
    result <- readFile "/tmp/yona_bench_write.txt"
    print (length result)
