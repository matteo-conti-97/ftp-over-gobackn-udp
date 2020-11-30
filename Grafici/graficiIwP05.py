import matplotlib.pyplot as plt

if __name__ == '__main__':

   y_td=[295200000, 13.5, 19.2, 17.09, 17.67]
   x_td=[1, 5, 20, 100, 1000]
   plt.xlabel("N")
   plt.ylabel("ms")
   plt.title("Get con P=0.5")
   plt.plot(x_td, y_td, 'b', label="T dinamico")
   y_t10 = [1446.4, 18.7, 9.8, 10.1, 61.3]
   x_t10 = [1, 5, 20, 100, 1000]
   plt.plot(x_t10, y_t10, 'r', label="T 10ms")
   y_t100 = [14348.16, 411, 17.3, 14.09, 6.85]
   x_t100 = [1, 5, 20, 100, 1000]
   plt.plot(x_t100, y_t100, 'g', label="T 100ms")
   y_t1000 = [121064.2, 2007.15, 8.45, 8.19, 8.7]
   x_t1000 = [1, 5, 20, 100, 1000]
   plt.plot(x_t1000, y_t1000, 'y', label="T 1000ms")
   plt.legend(["T dinamico", "T 10ms", "T 100ms", "T 1000ms"])
   plt.show()