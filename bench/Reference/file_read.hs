main :: IO ()
main = do
    content <- readFile "bench/data/bench_text.txt"
    print (length content)
