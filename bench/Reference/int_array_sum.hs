import Data.Array.Unboxed
main :: IO ()
main = do
  let a = listArray (0, 9999) [1..10000] :: UArray Int Int
  print (sum (elems a))
