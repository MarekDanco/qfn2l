all: .venv
	.venv/bin/python3 plot.py
	pdfcrop tangent_plane.pdf tangent_plane.pdf

.venv:
	python3 -m venv .venv
	.venv/bin/pip install -q -r requirements.txt
