#/sbin/sh

echo "Faking operation for 5 seconds."
sleep 5
RES=`imenu "Interactive Menu Test 1" "Answer no. 1" "Answer no. 2" "Answer no. 3" "Answer no. 4"`
echo "Answered the option no. $RES."

echo "Faking operation for 5 seconds."
sleep 5
RES=`imenu "Interactive Menu Test 2" "Answer no. 1" "Answer no. 2" "Answer no. 3" "Answer no. 4"`
echo "Answered the option no. $RES."
