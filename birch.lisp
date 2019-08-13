;; Initialize core bot variables.

(defq config-file "birch.lisp")

(defq msg-hook '(sed-line
		 command
		 log-line))

(defq join-hook '(init-channel
		  (lambda (serv chan)
		    (append "Hello " chan "!"))))

(defun symbol-protector (bind)
  (if (or (match "-hook$" "" (append (car bind)))
	  (string= (append (car bind)) "parse-sed"))
      (error (append "you aren't powerful enough to use `"
		     (car bind) "'"))
    (cdr bind)))

(defq symbol-hook symbol-protector)

;; Initialize global variables.

(defq trigger ",")
(defq should-log t)

(defun init ()
  "Prepare the bot for the main I/O loop."
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

(defun get-date (line) (nth line 0))
(defun get-nick (line) (nth line 1))
(defun get-body (line) (nth line 2))

(defun lispize-line (input)
  "Returns the expanded form of a message that contains embedded \
Lisp commands, or nil if it does not contain any embedded Lisp."
  (let ((idx 0)				; Current position in `input'.
	(len (length input))		; Length of the input.
	(output nil))			; Accumulated output.

    ;; Walk along the string incrementing `idx'.
    (while (< len idx)
      ;; If we encounter a `$(' at `idx' then we need to start
      ;; processing an expansion.
      (cond (string= (subseq input idx (+ idx 2)) "$(")
	    ;; Append the stuff leading up to this command to the
	    ;; output.
	    (setq output
		  (append (if output output "")
			  (subseq input 0 idx)))

	    ;; `read-string' returns both the parsed s-expression from
	    ;; its input and the remainder of the string (that wasn't
	    ;; parsed). We use `with-demoted-errors' so that if there
	    ;; is a parse error we'll get it as a string.
	    (let ((tmp (with-demoted-errors
			   (read-string
			    (subseq input (+ idx 1)))))
		  (expr (if (not (stringp tmp)) (car tmp))))
	      (if (stringp tmp)
		  (progn
		    ;; If `tmp' is a string then it's an error, so we
		    ;; should just append it to the output.
		    (setq output (append output tmp))
		    ;; And end the command processing. If there's a
		    ;; syntax error then the rest of the input is
		    ;; unusable.
		    (setq idx len)
		    (setq input ""))
		;; `input' is now whatever trails the final
		;; parenthesis in this expression. In "foo $(bar)
		;; baz", for instance, it would be " baz".
		(setq input (cdr tmp))
		;; Reset the index. It's incremented at the end of
		;; this loop and we actually want it to be 0 in
		;; the next iteration, so set it to -1.
		(setq idx (- 0 1))
		;; The length must also be updated because it's
		;; what `idx' is compared to.
		(setq len (length input))

		;; Finally, append the evaluation of the parsed
		;; command to the output. Always use `birch-eval' when
		;; evaluating user code!
		(setq output (append output (birch-eval expr)))))

	    ;; Set `should-log' to nil because we've (perhaps
	    ;; unsuccessfully) parsed a command from this
	    ;; message. (Commands aren't meant to be logged).
	    (setq should-log nil))

      ;; Move the index to the next character.
      (setq idx (+ idx 1)))

    ;; If the output exists then return output + line remainder,
    ;; otherwise nil.
    (if output
	(append output input))))

(defun command (line)
  "Processes any user commands embedded within a LINE that has been \
received from a server. Returns the result of the evaluation(s) or \
nil, if there were no commands embedded within the line."
  (let ((msg (nth line 2))
	(regex-match (match (append "^" trigger "(.*)$") "" msg))
	(regex-match (if regex-match regex-match
		       (match "^sudo\s+(.*)$" "" msg)))
	(cmd (nth regex-match 1)))
    (if cmd
	(let ((tmp (with-demoted-errors
		       (read-string (append "(" cmd ")"))))
	      (output (if (stringp tmp)
			  tmp
			(birch-eval (car tmp)))))
	  (setq should-log nil)
	  (if (and (not (stringp tmp))
		   (not (string= (cdr tmp) "")))
	      (append "error: trailing text in command: " (cdr tmp))
	    (if output output "()")))

      ;; There was no explicit command, but there might still be some
      ;; embedded Lisp.
      (cond (match "\$\(" "" msg)
	    (lispize-line msg)))))

(defun init-channel (serv chan)
  "Initialize all channel-specific structures in a specific channel. \
Does not initialize anything that has already been initialized. SERV \
and CHAN are the standard `join-hook' parameters."
  (let ((channel (append serv "/" chan)))
    (in channel progn
	;; Define the `log' variable if it does not already exist.
	(if (not (boundp 'log))
	    (defq log nil))

	;; Define the `raw-log' variable if it does not already exist.
	(if (not (boundp 'raw-log))
	    (defq raw-log nil)))))

(defun find-message (nick pattern &optional mode)
  "Find the last message in the current channel said by NICK that \
matches PATTERN with the mode string MODE or with the mode string \
\"\" if MODE is not given. Returns the body of the matching message, \
or nil if there was no matching message."
  (let ((node log)			;Current history line.
	(result nil))			;Return value.
    ;; Walk along the channel history, stopping only if we reach the
    ;; end of the history or we've found a result.
    (while (and node (not result))
      (let ((regex-match (match pattern
				(if mode mode "")
				(nth (car node) 2))))

	;; If the line matches PATTERN and line was said by NICK then
	;; set `result' to the body of the line.
	(if (and (if nick (string= (nth (car node) 1) nick) t)
		 regex-match)
	    (setq result (nth (car node) 2))))

      ;; Continue on to the next line.
      (setq node (cdr node)))
    result))

(defun find-line (nick pattern &optional mode)
  "Perform the same task as `find-message' but instead of returning \
the message, return the entire line representation (including nick \
and date). See the implementation of `find-message' for better \
documentation."
  (let ((node log)
	(result nil))
    (while (and node (not result))
      (let ((regex-match (match pattern
				(if mode mode "")
				(nth (car node) 2))))
	(if (and (if nick (string= (nth (car node) 1) nick) t)
		 regex-match)
	    (setq result (car node))))
      (setq node (cdr node)))
    result))

(defun spongebob (string)
  "Return the Spongebob translation of STRING."
  (sed "(.)(.)" "\l\1\u\2" "g" string))

(defmacro mock-user (nick &optional pattern)
  "Return the Spongebob translation of the last message said by NICK \
in the current channel that matches PATTERN, or nil if NICK has not \
said anything."
  (let ((string (if pattern
		    (find-message (append nick) pattern)
		  (find-message (append nick) ""))))
    (if string (spongebob string))))

(defmacro mock (&optional nick pattern)
  "Mock something someone said. The optional arguments NICK and \
PATTERN allow you to narrow down your mockage to a particular \
message."
  (let ((result (if (not nick)
		    ;; If there's no nick then do the last line.
		    (spongebob (nth (car log) 2))
		  ;; Otherwise actually mock the user.
		  (eval ~(mock-user ,nick ,pattern)))))
    (if result
	result
      (append "No matching message found for " nick "."))))

(defun sed-output (pattern replacement mode guy subject)
  (let ((result (sed pattern replacement mode (nth subject 2)))
	(prefix nil)
	(target (nth subject 1)))
    (setq prefix
	  (if (string= guy target)
	      (append guy " meant to say: ")
	    (append guy " thinks " target " meant to say: ")))
    (append prefix result)))

(defun parse-sed (string)
  "Parse the contents of STRING as a sed command. If there is no sed \
command in STRING then return nil, otherwise return a list \
containing these elements in order: target user, regex, substitution \
text, regex mode."
  (let ((regex-match
	 (match "^\s*(?:(\S+)[[:punct:]])?\s*\
s([[:punct:]\|])(.*\2.*)\2(\S*)\s*$"
		""
		string))
	(target (nth regex-match 1))
	(delimiter (nth regex-match 2))
	(input (nth regex-match 3))
	(mode (nth regex-match 4))
	(idx 0)
	(len (length input))
	(pattern "")
	(replacement nil))
    (cond regex-match
	  ;; Walk along the input string until we find the delimiter
	  ;; separating the pattern from the replacement,
	  ;; accumulating the pattern in `pattern' along the way.
	  (while (and (< len idx) (not replacement))
	    ;; If we encounter a backslash then we need to handle
	    ;; escaped delimiters.
	    (if (string= (nth input idx) "\\")
		(progn
		  (setq idx (+ idx 1))
		  (if (string= (nth input idx) delimiter)
		      (setq pattern (append pattern delimiter))
		    (setq pattern (append pattern "\\" (nth input idx)))))
	      ;; It's not an escaped delimiter, so either it's the
	      ;; delimiter or it's just a regular character.
	      (if (string= (nth input idx) delimiter)
		  (setq replacement (subseq input (+ idx 1)))
		(setq pattern (append pattern (nth input idx)))))
	    (setq idx (+ idx 1)))
	  (cond replacement
		(list (if (string= target "") nil target)
		      pattern
		      replacement
		      mode)))))

(defun sed-line (line)
  "If LINE represents a sed command then perform the intended \
substitution on the first line in the channel history that matches \
the pattern described by it."
  (let ((nick (get-nick line))
	(arguments (parse-sed (get-body line)))
	(target (nth arguments 0))
	(pattern (nth arguments 1))
	(replacement (nth arguments 2))
	(mode (nth arguments 3))
	(subject (cond arguments (find-line target pattern mode))))
    (cond arguments
	  (setq should-log nil)
	  (if subject
	      (sed-output pattern replacement mode nick subject)
	    (if target
		(append "No matching message found for " target
			" in the last " (length log) " messages.")
	      (append "No matching message found in the last "
		      (length log) " messages."))))))

(defun log-line (line)
  (if should-log
      (setq log (cons line log)))
  (setq should-log t)

  ;; Ignore `should-log' for this one.
  (setq raw-log (cons line raw-log))
  nil)

(defun format-line (line)
  "Produce a string representation of LINE in a log-like format."
  (append (nth line 0) " <" (nth line 1) "> " (nth line 2)))

(defun ping () '"pong")

(defun history ()
  (let ((node (reverse log))
	(output ""))
    (while node
      (setq output (append output (format-line (car node)) "\n"))
      (setq node (cdr node)))
    output))

(in "freenode/##c-offtopic" defq trigger "\.")
(in "kroknet/#test2" defq trigger "\.")
