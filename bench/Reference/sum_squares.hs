loop :: Int -> Int -> Int
loop 0 acc = acc
loop n acc = loop (n - 1) (acc + n * n)
main :: IO ()
main = print (loop 1000000 0)
