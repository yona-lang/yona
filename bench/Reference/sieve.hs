import Data.Array.Unboxed
sieve :: Int -> Int
sieve n = length [i | i <- [2..n], arr ! i]
  where
    arr :: UArray Int Bool
    arr = accumArray (\_ _ -> False) True (0, n)
          [(j, ()) | i <- [2..isqrt n], j <- [i*i, i*i+i..n]]
    isqrt = floor . sqrt . fromIntegral
main :: IO ()
main = print (sieve 100000)
