-- list_reverse: build 1..10000, reverse, report length.
main :: IO ()
main = print (length (reverse (go 10000 [])))
  where
    go 0 acc = acc
    go n acc = go (n - 1) (n : acc)
