;; Initialize core bot variables.

(defq config-file "birch.lisp")

(defq msg-hook '((lambda (line) (stdout (format-line line) "\n"))
		 ;(lambda (line) (stdout (mpanize (get-body line)) "\n"))
		 sed-line
		 command
		 log-line))

(defq join-hook '(init-channel
		  (lambda (serv chan)
		    (append "Hello " chan "!"))))

;; Initialize global variables.

(defq trigger ",")
(defq should-log t)
(defq recursion-limit 512)

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
  (join "kroknet" "#test2"))

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
  (let ((msg (get-body line))
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
	    (setq output (if output output "()"))
	    (stdout "===> " output "\n")
	    output))

      ;; There was no explicit command, but there might still be some
      ;; embedded Lisp.
      (cond (match "\$\(" "" msg)
	    (lispize-line msg)))))

(defun init-channel (serv chan)
  "Initialize all channel-specific structures in a specific channel. \
Does not initialize anything that has already been initialized. SERV \
and CHAN are the standard `join-hook' parameters."
  (in (append serv "/" chan) progn
      ;; Define the `log' variable if it does not already exist.
      (if (not (boundp 'log))
	  (defq log nil))

      ;; Define the `raw-log' variable if it does not already exist.
      (if (not (boundp 'raw-log))
	  (defq raw-log nil))))

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
  (sed "(?<!^)[[:alpha:]].*?([[:alpha:]]|$)" "\L\u\0" "g" string))

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
	 (match "^\s*(?:(\S+)\p{P})?\s*\
s(\p{P}|\p{S})(.*\2.*)\2(\S*)\s*$"
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
  (append (get-date line) " <" (get-nick line) "> " (get-body line)))

(defun ping () '"pong")

(defun history ()
  (let ((node (reverse raw-log))
	(output ""))
    (while node
      (setq output (append output (format-line (car node)) "\n"))
      (setq node (cdr node)))
    output))

(defun number-to-string (x)
  (let ((position '("" "" "" "thousand" "thousand" "thousand"
		    "million" "million" "million" "billion"
		    "billion" "billion"))
	(tens '("" "" "twenty-" "thirty-" "forty-" "fifty-"
		"sixty-" "seventy-" "eighty-" "ninety-"))
	(teens '("ten" "eleven" "twelve" "thirteen"
		 "fourteen" "fifteen" "sixteen" "seventeen"
		 "eighteen" "nineteen"))
	(naughts '("zero" "one" "two" "three" "four"
		   "five" "six" "seven" "eight" "nine")))
    2))

;; TODO this is the worst way possible to do this
(defun superscript (x)
  (setq x (append x))
  (setq x (sed "0" "⁰" "g" x))
  (setq x (sed "1" "¹" "g" x))
  (setq x (sed "2" "²" "g" x))
  (setq x (sed "3" "³" "g" x))
  (setq x (sed "4" "⁴" "g" x))
  (setq x (sed "5" "⁵" "g" x))
  (setq x (sed "6" "⁶" "g" x))
  (setq x (sed "7" "⁷" "g" x))
  (setq x (sed "8" "⁸" "g" x))
  (setq x (sed "9" "⁹" "g" x))
  x)

(defq mpan-counter 0)
(defq mpan-phrases '("¯\_(ツ)_/¯"
		     "— Also I am not sure this is necessary."
		     ":<"
		     "OTOH… who ever said they are round? :>"
		     ":>"
		     ":/"
		     "OTOH I wasn’t partying either."
		     "._."
		     "Also I find /IGNORE to be harmful."
		     "damnit, the bot is getting sentient!"
		     "ncurses*"
		     "IMO if someone blocks Tor by default, they should also block whole UK and PRC by default too."
		     "DISCRIMINATION!"
		     "O.o"
		     "LOL"
		     "xD"))

(defun mpan-phrase ()
  (let ((phrase (nth mpan-phrases mpan-counter)))
    (setq mpan-counter (% (+ mpan-counter 1) (length mpan-phrases)))
    phrase))

(defun mpanize (x)
  (cond (match "!\s*$" "" x)
  	(setq x (append x " \o/")))
  (cond (match "\blol\b" "i" x)
  	(setq x (append x " xD")))
  (setq x (sed "why[^.?]*$" "\0?" "" x))
  (setq x (sed "\.\s+(\w)" ". \u\1" "g" x))
  (setq x (sed "([^[:punct:]]|\))$" "\0." "" x))
  (setq x (append x " " (mpan-phrase)))
  (let ((n 1))
    (while (match "\(([[:word:]].*?)\)(?!\w)(.*)" "" x)
      (cond (= n 1) (setq x (append x " |")))
      (setq x (sed "\(([[:word:]].*?)\)(?!\w)(.*)"
  		   (append "⁽" (superscript n) "⁾\2"
  			   (if (> 1 n) " " "")
  			   (superscript n) " \1")
  		   ""
  		   x))
      (setq n (+ n 1))))
  (setq x (sed "([^[:punct:]]|\))$" "\0." "" x))
  (setq x (sed "\bi\b" "I" "g" x))
  (setq x (sed "^\s*(\w)" "\u\1" "" x))
  (setq x (sed "\"\b" "“" "g" x))
  (setq x (sed "\b\"" "”" "g" x))
  (setq x (sed "(\S)'(\S)" "\1’\2" "g" x))
  (setq x (sed "'\b" "‘" "g" x))
  (setq x (sed "\b'" "’" "g" x))
  (setq x (sed "[[:punct:]]?\s+http" ": http" "g" x))
  (setq x (sed "http\S+(?<!\.)" "<\0>" "g" x))
  (setq x (sed "[⁰¹²³⁴⁵⁶⁷⁸⁹]\s+\w" "\U\0" "g" x))
  (setq x (sed "(\w)\s+([⁰¹²³⁴⁵⁶⁷⁸⁹])" "\1. \2" "g" x))
  (setq x (sed "\b(no|yes)\b(?![[:punct:]])" "\0," "gi" x))
  (setq x (sed "linux" "GNU/Linux" "gi" x))
  (setq x (sed "\bgnu\b" "GNU" "gi" x))
  (setq x (sed "\btfw\b" "TFW" "gi" x))
  (setq x (sed "\bmfw\b" "MFW" "gi" x))
  (setq x (sed "\bbtw\b" "BTW" "gi" x))
  (setq x (sed "\bofc\b" "OFC" "gi" x))
  (setq x (sed "\boh no\b" "ono" "gi" x))
  (setq x (sed "\bono\b" "ONO" "gi" x))
  (setq x (sed "\blo+l\b" "LOL" "gi" x))
  (setq x (sed "\blol\s+" "LOL, " "gi" x))
  (setq x (sed "\birc\s+" "IRC" "gi" x))
  (setq x (sed "\$" "zł" "g" x))
  (setq x (sed "£" "zł" "g" x))
  (setq x (sed "\s+-+\s+" "—" "g" x))
  (setq x (sed "\s+" " " "g" x))
  (setq x (sed "fahrenheit" "Celsius" "gi" x))
  (setq x (sed "\.{2,}" "…" "gi" x))
  (setq x (sed ":\)|:D" "\\\\:D/" "g" x))
  (setq x (sed "\bc\b" "C" "gi" x))
  x)

(defmacro mpan-user (nick &optional pattern)
  "Return the mpan translation of the last message said by NICK \
in the current channel that matches PATTERN, or nil if NICK has not \
said anything."
  (let ((string (if pattern
		    (find-message (append nick) pattern)
		  (find-message (append nick) ""))))
    (if string (mpanize string))))

(defmacro mpan (&optional nick pattern)
  "mpan something someone said. The optional arguments NICK and \
PATTERN allow you to narrow down your mpanage to a particular \
message."
  (let ((result (if (not nick)
		    ;; If there's no nick then do the last line.
		    (mpanize (nth (car log) 2))
		  ;; Otherwise actually mpan the user.
		  (eval ~(mpan-user ,nick ,pattern)))))
    (if result
	result
      (append "No matching message found for " nick "."))))

(in "freenode/##c-offtopic" defq trigger "\.")
(in "kroknet/#test2" defq trigger "\.")
