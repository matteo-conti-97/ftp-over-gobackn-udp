import matplotlib.pyplot as plt

if __name__ == '__main__':

   y_td=[634920000, 24662.69, 28.3, 22.4, 22.3]
   x_td=[1, 5, 20, 100, 1000]
   plt.xlabel("N")
   plt.ylabel("ms")
   plt.title("Get con P=0.8")
   plt.plot(x_td, y_td, 'b', label="T dinamico")
   y_t10 = [5350, 260, 13.11, 23.8, 83.82]
   x_t10 = [1, 5, 20, 100, 1000]
   plt.plot(x_t10, y_t10, 'r', label="T 10ms")
   y_t100 = [59551.7, 2707.3, 23.54, 22.62, 14.7]
   x_t100 = [1, 5, 20, 100, 1000]
   plt.plot(x_t100, y_t100, 'g', label="T 100ms")
   y_t1000 = [563159.4, 20005, 1002.2, 23.54, 24.53]
   x_t1000 = [1, 5, 20, 100, 1000]
   plt.plot(x_t1000, y_t1000, 'y', label="T 1000ms")
   plt.legend(["T dinamico", "T 10ms", "T 100ms", "T 1000ms"])
   plt.show()