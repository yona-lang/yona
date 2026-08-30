tak :: Int -> Int -> Int -> Int
tak x y z
  | y >= x = z
  | otherwise = tak (tak (x-1) y z) (tak (y-1) z x) (tak (z-1) x y)
main :: IO ()
main = print (tak 30 20 10)
