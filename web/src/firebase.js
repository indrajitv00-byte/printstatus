import { initializeApp } from "firebase/app";
import { getAuth } from "firebase/auth";
import { getDatabase } from "firebase/database";

const firebaseConfig = {
  apiKey: "AIzaSyDoqgsxBkZDTWCenP5dW_CDlR8Q7iItujQ",
  authDomain: "printerstatus-3619b.firebaseapp.com",
  projectId: "printerstatus-3619b",
  appId: "1:915116107558:web:f3bee49b8649d0a5efc3e7",
  // Firebase console > Realtime Database > Data tab: the database URL shown at the top. Paste it here.
  databaseURL: "https://printerstatus-3619b-default-rtdb.asia-southeast1.firebasedatabase.app/",
};

const app = initializeApp(firebaseConfig);
export const auth = getAuth(app);
export const db = getDatabase(app);
