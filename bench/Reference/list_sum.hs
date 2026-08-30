-- list_sum: sum 1..10000 via explicit cons-list build + foldl.
main :: IO ()
main = print (foldl (+) (0 :: Int) (go 10000 []))
  where
    go 0 acc = acc
    go n acc = go (n - 1) (n : acc)
