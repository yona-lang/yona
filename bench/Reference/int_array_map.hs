import Data.Array.Unboxed
main :: IO ()
main = do
  let a  = listArray (0, 9999) [1..10000] :: UArray Int Int
      ds = listArray (0, 9999) (map (*2) (elems a)) :: UArray Int Int
  print (sum (elems ds))
