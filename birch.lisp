(defq config-file "birch.lisp")
(defq msg-hook '(sed-line command log-line))
(defq join-hook '(init-log
		  (lambda (serv chan)
		    (append "Hello " chan "!"))))
(defq trigger ",")
(defq sed-regex "^\s*(?:(\w+):\s*)?s/(.*?)/(.*?)/(\w*)\s*$")
(defq can-log t)
(defun init ()
  (connect "kroknet"
	   "localhost"
	   1221
	   "birch"
	   "birch"
	   "realname")
  ;; (connect "freenode"
  ;; 	   "irc.freenode.net"
  ;; 	   6667
  ;; 	   "birch"
  ;; 	   "birch"
  ;; 	   "realname")
  (join "kroknet" "#test")
  (join "kroknet" "#test2")
  (join "freenode" "##krok"))
(defun command (line)
  (let ((cmd nil)
	(m nil)
	(msg (nth line 2))
	(output nil))
    (if (setq m (match (append "^" trigger "(.*)$") "" msg))
	(setq cmd (nth m 1)))
    (if (and (not cmd) (setq m (match "^sudo\s+(.*)$" "" msg)))
	(setq cmd (nth m 1)))
    (if cmd
	(progn
	  (setq output
		(with-demoted-errors
		    (eval ~(in (append (current-server)
				       "/"
				       (current-channel))
			       eval (read-string (append "(" ,cmd ")"))))))
	  (setq can-log nil)
	  (if output output "()"))
      (let ((i 0) (len (length msg)))
      	(while (< len i)
      	  (if (string= (subseq msg i (+ i 2)) "$(")
      	      (progn
      		(setq i (+ i 1))
      		(setq output
		      (append (if output (append output ", ") "")
			      (with-demoted-errors
				  (eval ~(in (append (current-server)
						     "/"
						     (current-channel))
					     eval (read-string ,(subseq msg i)))))))
      		(setq can-log nil)))
      	  (setq i (+ i 1)))
	(if output (setq output (append (nth line 1) ": " output)))
      	(if (not can-log)
      	    (if output output "()"))))))
(defun init-log (serv chan)
  (in (append serv "/" chan)
      if (not (boundp 'log))
      (defq log nil)))
(defun find-message (nick pattern)
  (defq node log)
  (defq m nil)
  (defq result nil)
  (while (and node (not m))
    (setq m (match pattern "" (nth (car node) 2)))
    (if (and (string= (nth (car node) 1) nick) m)
	(setq result (nth (car node) 2)))
    (setq node (cdr node)))
  result)
(defun spongebob (x)
  (sed "(.)(.)" "\l\1\u\2" "g" x))
(defmacro mock-user (nick &optional pattern)
  (defq subject
    (if pattern
	(find-message (append nick) pattern)
      (find-message (append nick) "")))
  (if subject (spongebob subject)))
(defmacro mock (&optional nick pattern)
  (if (not nick)
      (defq result (spongebob (nth (car log) 2)))
    (eval ~(defq result (mock-user ,nick ,pattern))))
  (if result result
    (append "No matching message found for " nick ".")))
(defun sed-line (line)
  (let ((re (match sed-regex "" (nth line 2)))
	(output nil))
    (if re
	(progn
	  (defq nick
	    (if (not (string= (nth re 1) ""))
		(nth re 1)
	      nil))
	  (defq node log)
	  (defq m nil)
	  (while (and (not m) node)
	    (setq m (match (nth re 2) (nth re 4) (nth (car node) 2)))
	    (if (match sed-regex "" (nth (car node) 2))
		(setq m nil))
	    (if (and nick (not (string= nick (nth (car node) 1))))
		(setq m nil))
	    (if m
		(progn
		  (defq result (sed (nth re 2)
				    (nth re 3)
				    (nth re 4)
				    (nth (car node) 2)))
		  (if (string= (nth line 1) (nth (car node) 1))
		      (setq output (append (nth line 1)
					   " meant to say: "
					   result))
		    (setq output (append (nth line 1)
					 " thinks "
					 (nth (car node) 1)
					 " meant to say: "
					 result)))))
	    (setq node (cdr node)))
	  (if (not m)
	      (if nick
		  (setq output (append "No matching message found for "
				       nick
				       " in the last "
				       (length log)
				       " messages."))
		(setq output (append "No matching message found in the last "
				     (length log)
				     " messages."))))))
    output))
(defun log-line (line)
  (if can-log
      (setq log (cons line log)))
  (setq can-log t)
  nil)
(defun print-line (x)
  (append (nth x 0) " <" (nth x 1) "> " (nth x 2)))
(defun ping () '"pong")
(defun history ()
  (let ((node (reverse log))
	(output ""))
    (while node
      (setq output (append output (print-line (car node)) "\n"))
      (setq node (cdr node)))
    output))
(in "freenode/##c-offtopic" progn
    (defq trigger "\."))
(in "kroknet/#test2" progn
    (defq trigger "\."))
