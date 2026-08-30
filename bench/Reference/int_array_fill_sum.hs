import Data.Array.Unboxed
main :: IO ()
main = do
  let a = listArray (0, 9999) (replicate 10000 7) :: UArray Int Int
  print (sum (elems a))
