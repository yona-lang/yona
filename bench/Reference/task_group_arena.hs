main :: IO ()
main = print (loop 50000 0)
  where
    loop 0 acc = acc
    loop n acc = loop (n - 1) (acc + 10)
