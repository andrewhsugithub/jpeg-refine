INPUT_IMAGE=test.jpg
OPTIMIZED_IMAGE=optimized.jpg
REQUANTIZED_IMAGE=requantized.jpg
QUALITY=10

run: optimize requantize

optimize:
	g++ -std=c++17 -o opt optimize.cpp -ljpeg
	./opt $(INPUT_IMAGE) $(OPTIMIZED_IMAGE)
	rm ./opt

requantize:
	g++ -std=c++17 -o requant requantize.cpp -ljpeg
	./requant $(INPUT_IMAGE) $(REQUANTIZED_IMAGE) $(QUALITY)
	rm ./requant