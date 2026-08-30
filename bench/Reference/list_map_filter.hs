-- list_map_filter: map (*2) + filter (`mod` 4 == 0) on 1..10000, sum.
main :: IO ()
main = print (sum [x * 2 | x <- go 10000 [], (x * 2) `mod` 4 == 0])
  where
    go 0 acc = acc
    go n acc = go (n - 1) (n : acc)
