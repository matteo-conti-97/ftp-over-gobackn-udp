import matplotlib.pyplot as plt

if __name__ == '__main__':

   y_td=[11.78, 7.46, 11.27, 23.12, 127.32]
   x_td=[1, 5, 20, 100, 1000]
   plt.xlabel("N")
   plt.ylabel("ms")
   plt.title("Get con P=0.1")
   plt.plot(x_td, y_td, 'b', label="T dinamico")
   y_t10 = [226.32, 1.89, 1.97, 2.23, 3.89]
   x_t10 = [1, 5, 20, 100, 1000]
   plt.plot(x_t10, y_t10, 'r', label="T 10ms")
   y_t100 = [1318.25, 2.68, 2.23, 2.74, 3.44]
   x_t100 = [1, 5, 20, 100, 1000]
   plt.plot(x_t100, y_t100, 'g', label="T 100ms")
   y_t1000 = [14016.4, 2.12, 2.75, 3.96, 3.95]
   x_t1000 = [1, 5, 20, 100, 1000]
   plt.plot(x_t1000, y_t1000, 'y', label="T 1000ms")
   plt.legend(["T dinamico", "T 10ms", "T 100ms", "T 1000ms"])
   plt.show()