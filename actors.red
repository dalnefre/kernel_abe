(define | (template (x) (list x)))
(define ; (template (x y) (prepend x y)))
(define , (template (x) (first x)))
(define . (template (x) (rest x)))
(define ., (template (x) (first (rest x))))
(define .. (template (x) (rest (rest x))))
(define ,. (template (x) (rest (first x))))
(define ,, (template (x) (first (first x))))
(define .., (template (x) (first (rest (rest x)))))
(define ... (template (x) (rest (rest (rest x)))))
(define say (actor (function what (send @print (; '"Say, " what))) ))
(define speak (actor ((function (label) (function what (send @print (; label what)))) '"Speak, ") ))
(define a_keep (function (hold) (function msg (seq (send @print (| msg '+ hold)) (become (a_keep (; hold msg))))) ))
(define keep (actor (a_keep ()) ))
(define xyzzy 'plugh)
(define magic (actor (function () (define xyzzy 'plover)) ))
'(Actors Loaded)